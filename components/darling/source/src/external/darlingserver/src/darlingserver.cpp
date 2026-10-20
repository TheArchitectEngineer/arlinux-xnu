/**
 * This file is part of Darling.
 *
 * Copyright (C) 2021 Darling developers
 *
 * Darling is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Darling is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Darling.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE 1
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <sys/prctl.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <iostream>
#include <linux/sched.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/signal.h>
#include <filesystem>

#include <darling-config.h>

#include <darlingserver/server.hpp>
#include <darlingserver/config.hpp>

#ifndef DARLINGSERVER_INIT_PROCESS
	#define DARLINGSERVER_INIT_PROCESS "/sbin/launchd"
#endif

#ifndef DARLINGSERVER_XDG_USER_DIR_CMD
	#define DARLINGSERVER_XDG_USER_DIR_CMD "xdg-user-dir"
#endif

#if DSERVER_ASAN
	#include <sanitizer/lsan_interface.h>
#endif

// TODO: most of the code here was ported over from startup/darling.c; we should C++-ify it.

static void temp_drop_privileges(uid_t uid, gid_t gid);
static void regain_privileges();

// Opens a directory of the user's prefix for root-mode setup. The path is resolved with the user's
// privileges; the directory must be owned by the user, or by root when rootOwnedOK.
static int openPrefixDir(int dirFD, const char* path, int flags, bool rootOwnedOK, uid_t originalUID, gid_t originalGID)
{
	temp_drop_privileges(originalUID, originalGID);
	int fd = openat(dirFD, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | flags);
	if (fd < 0) {
		fprintf(stderr, "Cannot open directory %s: %s\n", path, strerror(errno));
		exit(1);
	}
	regain_privileges();

	struct stat st;
	if (fstat(fd, &st) != 0 || (st.st_uid != originalUID && !(rootOwnedOK && st.st_uid == 0))) {
		fprintf(stderr, "%s is not owned by the invoking user\n", path);
		exit(1);
	}
	return fd;
}

// Run as the user before the overlay is mounted: creates each directory of the lower tree in the upper one,
// so the merged prefix directories belong to the user without root changing any owner. Takes ownership of lowerFD.
static void mirrorDirectoryTree(int lowerFD, int upperFD)
{
	DIR* dir = fdopendir(lowerFD);
	if (!dir) {
		close(lowerFD);
		return;
	}

	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL)
	{
		struct stat st;
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0
			|| fstatat(dirfd(dir), ent->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(st.st_mode))
			continue;

		// Undo the umask through the opened directory, never by resolving the name again.
		bool created = mkdirat(upperFD, ent->d_name, 0700) == 0;
		if (!created && errno != EEXIST)
			fprintf(stderr, "Cannot create prefix directory %s: %s\n", ent->d_name, strerror(errno));

		int lowerChild = openat(dirfd(dir), ent->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		int upperChild = openat(upperFD, ent->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (lowerChild >= 0 && upperChild >= 0)
			mirrorDirectoryTree(lowerChild, upperChild);
		else if (lowerChild >= 0)
			close(lowerChild);
		if (upperChild >= 0) {
			if (created && fchmod(upperChild, st.st_mode & ALLPERMS) != 0) {
				fprintf(stderr, "Cannot set prefix directory permissions: %s\n", strerror(errno));
				exit(1);
			}
			close(upperChild);
		}
	}

	closedir(dir);
}

const char* xdgDirectory(const char* name)
{
	static char dir[4096];
#define DARLING_XDG_REDIRECT " 2>/dev/null"
	size_t cmd_len = sizeof(DARLINGSERVER_XDG_USER_DIR_CMD " " DARLING_XDG_REDIRECT) + strlen(name);
	char* cmd = (char*) malloc(cmd_len);
	if (!cmd) return "";
	snprintf(cmd, cmd_len, DARLINGSERVER_XDG_USER_DIR_CMD " %s" DARLING_XDG_REDIRECT, name);

	FILE* proc = popen(cmd, "r");

	free(cmd);

	if (!proc)
		return NULL;

	fgets(dir, sizeof(dir)-1, proc);

	pclose(proc);

	size_t len = strlen(dir);
	if (len <= 1)
		return NULL;

	if (dir[len-1] == '\n')
		dir[len-1] = '\0';
	return dir;
}

void setupUserHome(const char* prefix, uid_t originalUID)
{
	char buf[4096], buf2[4096];

	snprintf(buf, sizeof(buf), "%s/Users", prefix);

	// Remove the old /Users symlink that may exist
	unlink(buf);

	// mkdir /Users
	mkdir(buf, 0777);

	// mkdir /Users/Shared
	strcat(buf, "/Shared");
	mkdir(buf, 0777);

	const char* home = getenv("HOME");

	const char* login = NULL;
	struct passwd* pw = getpwuid(originalUID);

	if (pw != NULL)
		login = pw->pw_name;

	if (!login)
		login = getlogin();

	if (!login)
	{
		fprintf(stderr, "Cannot determine your user name\n");
		exit(1);
	}
	if (!home)
	{
		fprintf(stderr, "Cannot determine your home directory\n");
		exit(1);
	}

	snprintf(buf, sizeof(buf), "%s/Users/%s", prefix, login);

	// mkdir /Users/$LOGIN
	mkdir(buf, 0755);

	snprintf(buf2, sizeof(buf2), "/Volumes/SystemRoot%s", home);

	strcat(buf, "/LinuxHome");
	unlink(buf);

	// symlink /Users/$LOGIN/LinuxHome -> $HOME
	symlink(buf2, buf);

	static const char* xdgmap[][2] = {
		{ "DESKTOP", "Desktop" },
		{ "DOWNLOAD", "Downloads" },
		{ "PUBLICSHARE", "Public" },
		{ "DOCUMENTS", "Documents" },
		{ "MUSIC", "Music" },
		{ "PICTURES", "Pictures" },
		{ "VIDEOS", "Movies" },
	};

	for (int i = 0; i < sizeof(xdgmap) / sizeof(xdgmap[0]); i++)
	{
		const char* dir = xdgDirectory(xdgmap[i][0]);
		if (!dir)
			continue;

		snprintf(buf2, sizeof(buf2), "/Volumes/SystemRoot%s", dir);
		snprintf(buf, sizeof(buf), "%s/Users/%s/%s", prefix, login, xdgmap[i][1]);

		unlink(buf);
		symlink(buf2, buf);
	}
}

void setupCoredumpPattern(void)
{
	FILE* f = fopen("/proc/sys/kernel/core_pattern", "w");
	if (f != NULL)
	{
		// This is how macOS saves core dumps
		fputs("/cores/core.%p\n", f);
		fclose(f);
	}
}

static void wipeDir(const char* dirpath)
{
	char path[4096];
	struct dirent* ent;
	DIR* dir = opendir(dirpath);

	if (!dir)
		return;

	while ((ent = readdir(dir)) != NULL)
	{
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", dirpath, ent->d_name);

		if (ent->d_type == DT_DIR)
		{
			wipeDir(path);
			rmdir(path);
		}
		else if (ent->d_type == DT_SOCK)
		{
			/* Leave sockets alone. wipeDir runs on every container start, and
			 * /var/run and /var/tmp are where out-of-container services put
			 * their listeners - a host service binding a socket under the
			 * prefix has it deleted out from under it at every boot, so a guest
			 * can never connect to it. Nothing here owns a socket: the
			 * container's own state in these directories is files and
			 * directories, and the one socket the container itself needs
			 * (shellspawn.sock) is created after this runs. */
		}
		else
			unlink(path);
	}

	closedir(dir);
}

static void ensureProcSymlink(const char* prefixPath)
{
	char procPath[4096];
	struct stat st;
	snprintf(procPath, sizeof(procPath), "%s/proc", prefixPath);
	if (lstat(procPath, &st) == 0)
	{
		if (S_ISDIR(st.st_mode))
		{
			rmdir(procPath);
		}
		else if (S_ISLNK(st.st_mode))
		{
			char target[256];
			ssize_t len = readlink(procPath, target, sizeof(target) - 1);
			if (len > 0)
			{
				target[len] = '\0';
				if (strcmp(target, "/Volumes/SystemRoot/proc") != 0)
					unlink(procPath);
			}
		}
	}
	if (lstat(procPath, &st) != 0)
	{
		symlink("/Volumes/SystemRoot/proc", procPath);
	}
}

static void ensureShSymlink(const char* prefixPath)
{
	char binDir[4096];
	snprintf(binDir, sizeof(binDir), "%s/bin", prefixPath);
	mkdir(binDir, 0755);

	char shPath[4096];
	char bashPath[4096];
	struct stat st;
	snprintf(shPath, sizeof(shPath), "%s/bin/sh", prefixPath);
	snprintf(bashPath, sizeof(bashPath), "%s/bin/bash", prefixPath);

	const char* target = "bash";
	if (access(bashPath, X_OK) != 0)
	{
		char zshPath[4096];
		snprintf(zshPath, sizeof(zshPath), "%s/bin/zsh", prefixPath);
		if (access(zshPath, X_OK) == 0)
			target = "zsh";
	}

	if (stat(shPath, &st) != 0)
	{
		if (lstat(shPath, &st) == 0)
			unlink(shPath);
		symlink(target, shPath);
	}
}

static void relativizeNanobrewSymlinks(const char* dirPath)
{
	DIR* dir = opendir(dirPath);
	if (!dir) return;

	struct dirent* de;
	while ((de = readdir(dir)) != NULL)
	{
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;

		char binItem[4096];
		snprintf(binItem, sizeof(binItem), "%s/%s", dirPath, de->d_name);
		struct stat itemSt;
		if (lstat(binItem, &itemSt) == 0 && S_ISLNK(itemSt.st_mode))
		{
			char itemTarget[4096];
			ssize_t tlen = readlink(binItem, itemTarget, sizeof(itemTarget) - 1);
			if (tlen > 0)
			{
				itemTarget[tlen] = '\0';
				const char* pfxMatch = "/opt/nanobrew/prefix/";
				size_t pfxLen = strlen(pfxMatch);
				if (strncmp(itemTarget, pfxMatch, pfxLen) == 0)
				{
					char relTarget[4096];
					snprintf(relTarget, sizeof(relTarget), "../%s", itemTarget + pfxLen);
					unlink(binItem);
					symlink(relTarget, binItem);
				}
			}
		}
	}
	closedir(dir);
}

static void createDir(const char* path)
{
	struct stat st;
	if (stat(path, &st) == 0)
	{
		if (!S_ISDIR(st.st_mode))
		{
			fprintf(stderr, "%s already exists and is a file.\n", path);
		}
	}
	else if (errno == ENOENT)
	{
		if (mkdir(path, 0755) != 0 && errno != EEXIST)
		{
			fprintf(stderr, "Cannot create %s: %s\n", path, strerror(errno));
		}
	}
}

static void ensureHomebrewSymlinks(const char* prefixPath)
{
	char nanobrewBin[4096];
	snprintf(nanobrewBin, sizeof(nanobrewBin), "%s/opt/nanobrew/prefix/bin", prefixPath);

	struct stat st;
	if (stat(nanobrewBin, &st) != 0 || !S_ISDIR(st.st_mode))
		return;

	char nanobrewOpt[4096];
	snprintf(nanobrewOpt, sizeof(nanobrewOpt), "%s/opt/nanobrew/prefix/opt", prefixPath);
	relativizeNanobrewSymlinks(nanobrewBin);
	relativizeNanobrewSymlinks(nanobrewOpt);

	// 1. Ensure /opt/homebrew -> nanobrew/prefix
	char optDir[4096];
	snprintf(optDir, sizeof(optDir), "%s/opt", prefixPath);
	createDir(optDir);

	char optHomebrew[4096];
	snprintf(optHomebrew, sizeof(optHomebrew), "%s/opt/homebrew", prefixPath);
	if (lstat(optHomebrew, &st) == 0)
	{
		if (S_ISLNK(st.st_mode))
		{
			char target[4096];
			ssize_t len = readlink(optHomebrew, target, sizeof(target) - 1);
			if (len > 0)
			{
				target[len] = '\0';
				if (strcmp(target, "nanobrew/prefix") != 0)
				{
					unlink(optHomebrew);
					symlink("nanobrew/prefix", optHomebrew);
				}
			}
		}
	}
	else
	{
		symlink("nanobrew/prefix", optHomebrew);
	}

	// 2. Ensure /usr/local, /usr/local/bin, /usr/local/opt, /usr/local/Cellar
	char usrLocalDir[4096];
	snprintf(usrLocalDir, sizeof(usrLocalDir), "%s/usr/local", prefixPath);
	createDir(usrLocalDir);

	char usrLocalBin[4096];
	snprintf(usrLocalBin, sizeof(usrLocalBin), "%s/usr/local/bin", prefixPath);
	createDir(usrLocalBin);

	char usrLocalOpt[4096];
	snprintf(usrLocalOpt, sizeof(usrLocalOpt), "%s/usr/local/opt", prefixPath);
	if (lstat(usrLocalOpt, &st) == 0)
	{
		if (S_ISDIR(st.st_mode))
		{
			DIR* d = opendir(usrLocalOpt);
			if (d)
			{
				struct dirent* de;
				bool has_real_content = false;
				while ((de = readdir(d)) != NULL)
				{
					if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
						continue;
					if (strcmp(de->d_name, "opt") == 0)
					{
						char dummy[4096];
						snprintf(dummy, sizeof(dummy), "%s/opt", usrLocalOpt);
						unlink(dummy);
						continue;
					}
					has_real_content = true;
					break;
				}
				closedir(d);
				if (!has_real_content)
				{
					rmdir(usrLocalOpt);
					symlink("../../opt/nanobrew/prefix/opt", usrLocalOpt);
				}
			}
		}
		else if (S_ISLNK(st.st_mode))
		{
			char target[4096];
			ssize_t len = readlink(usrLocalOpt, target, sizeof(target) - 1);
			if (len > 0)
			{
				target[len] = '\0';
				if (strcmp(target, "../../opt/nanobrew/prefix/opt") != 0 && strcmp(target, "/opt/nanobrew/prefix/opt") != 0)
				{
					unlink(usrLocalOpt);
					symlink("../../opt/nanobrew/prefix/opt", usrLocalOpt);
				}
			}
		}
	}
	else
	{
		symlink("../../opt/nanobrew/prefix/opt", usrLocalOpt);
	}

	char usrLocalCellar[4096];
	snprintf(usrLocalCellar, sizeof(usrLocalCellar), "%s/usr/local/Cellar", prefixPath);
	if (lstat(usrLocalCellar, &st) == 0)
	{
		if (S_ISDIR(st.st_mode))
		{
			DIR* d = opendir(usrLocalCellar);
			if (d)
			{
				struct dirent* de;
				bool has_real_content = false;
				while ((de = readdir(d)) != NULL)
				{
					if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
						continue;
					has_real_content = true;
					break;
				}
				closedir(d);
				if (!has_real_content)
				{
					rmdir(usrLocalCellar);
					symlink("../../opt/nanobrew/prefix/Cellar", usrLocalCellar);
				}
			}
		}
		else if (S_ISLNK(st.st_mode))
		{
			char target[4096];
			ssize_t len = readlink(usrLocalCellar, target, sizeof(target) - 1);
			if (len > 0)
			{
				target[len] = '\0';
				if (strcmp(target, "../../opt/nanobrew/prefix/Cellar") != 0 && strcmp(target, "/opt/nanobrew/prefix/Cellar") != 0)
				{
					unlink(usrLocalCellar);
					symlink("../../opt/nanobrew/prefix/Cellar", usrLocalCellar);
				}
			}
		}
	}
	else
	{
		symlink("../../opt/nanobrew/prefix/Cellar", usrLocalCellar);
	}

	char usrLocalEtc[4096];
	snprintf(usrLocalEtc, sizeof(usrLocalEtc), "%s/usr/local/etc", prefixPath);
	if (lstat(usrLocalEtc, &st) == 0)
	{
		if (S_ISDIR(st.st_mode))
		{
			DIR* d = opendir(usrLocalEtc);
			if (d)
			{
				struct dirent* de;
				bool has_real_content = false;
				while ((de = readdir(d)) != NULL)
				{
					if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
						continue;
					has_real_content = true;
					break;
				}
				closedir(d);
				if (!has_real_content)
				{
					rmdir(usrLocalEtc);
					symlink("../../opt/nanobrew/prefix/etc", usrLocalEtc);
				}
			}
		}
		else if (S_ISLNK(st.st_mode))
		{
			char target[4096];
			ssize_t len = readlink(usrLocalEtc, target, sizeof(target) - 1);
			if (len > 0)
			{
				target[len] = '\0';
				if (strcmp(target, "../../opt/nanobrew/prefix/etc") != 0 && strcmp(target, "/opt/nanobrew/prefix/etc") != 0)
				{
					unlink(usrLocalEtc);
					symlink("../../opt/nanobrew/prefix/etc", usrLocalEtc);
				}
			}
		}
	}
	else
	{
		symlink("../../opt/nanobrew/prefix/etc", usrLocalEtc);
	}

	char usrLocalShare[4096];
	snprintf(usrLocalShare, sizeof(usrLocalShare), "%s/usr/local/share", prefixPath);
	if (lstat(usrLocalShare, &st) == 0)
	{
		if (S_ISDIR(st.st_mode))
		{
			DIR* d = opendir(usrLocalShare);
			if (d)
			{
				struct dirent* de;
				bool has_real_content = false;
				while ((de = readdir(d)) != NULL)
				{
					if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
						continue;
					has_real_content = true;
					break;
				}
				closedir(d);
				if (!has_real_content)
				{
					rmdir(usrLocalShare);
					symlink("../../opt/nanobrew/prefix/share", usrLocalShare);
				}
			}
		}
		else if (S_ISLNK(st.st_mode))
		{
			char target[4096];
			ssize_t len = readlink(usrLocalShare, target, sizeof(target) - 1);
			if (len > 0)
			{
				target[len] = '\0';
				if (strcmp(target, "../../opt/nanobrew/prefix/share") != 0 && strcmp(target, "/opt/nanobrew/prefix/share") != 0)
				{
					unlink(usrLocalShare);
					symlink("../../opt/nanobrew/prefix/share", usrLocalShare);
				}
			}
		}
	}
	else
	{
		symlink("../../opt/nanobrew/prefix/share", usrLocalShare);
	}

	// 3. Symlink all binaries from /opt/nanobrew/prefix/bin into /usr/local/bin
	DIR* dir = opendir(nanobrewBin);
	if (dir)
	{
		struct dirent* de;
		while ((de = readdir(dir)) != NULL)
		{
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
				continue;

			char linkPath[4096];
			snprintf(linkPath, sizeof(linkPath), "%s/%s", usrLocalBin, de->d_name);

			char expectedTarget[4096];
			snprintf(expectedTarget, sizeof(expectedTarget), "../../../opt/nanobrew/prefix/bin/%s", de->d_name);

			if (lstat(linkPath, &st) == 0)
			{
				if (S_ISLNK(st.st_mode))
				{
					char curTarget[4096];
					ssize_t len = readlink(linkPath, curTarget, sizeof(curTarget) - 1);
					if (len > 0)
					{
						curTarget[len] = '\0';
						if (strcmp(curTarget, expectedTarget) == 0)
							continue;
					}
					unlink(linkPath);
				}
				else
				{
					continue;
				}
			}

			symlink(expectedTarget, linkPath);
		}
		closedir(dir);
	}

	// 4. Python and Pip aliases and /usr/bin/python3
	char nbPython3[4096];
	snprintf(nbPython3, sizeof(nbPython3), "%s/python3", nanobrewBin);
	if (lstat(nbPython3, &st) == 0)
	{
		char usrBinPython3[4096];
		snprintf(usrBinPython3, sizeof(usrBinPython3), "%s/usr/bin/python3", prefixPath);
		if (lstat(usrBinPython3, &st) != 0)
		{
			symlink("../../opt/nanobrew/prefix/bin/python3", usrBinPython3);
		}

		char usrLocalPython[4096];
		snprintf(usrLocalPython, sizeof(usrLocalPython), "%s/python", usrLocalBin);
		if (lstat(usrLocalPython, &st) != 0)
		{
			symlink("python3", usrLocalPython);
		}
	}

	char nbPip3[4096];
	snprintf(nbPip3, sizeof(nbPip3), "%s/pip3", nanobrewBin);
	if (lstat(nbPip3, &st) == 0)
	{
		char usrLocalPip[4096];
		snprintf(usrLocalPip, sizeof(usrLocalPip), "%s/pip", usrLocalBin);
		if (lstat(usrLocalPip, &st) != 0)
		{
			symlink("pip3", usrLocalPip);
		}
	}

	// 5. Ensure /etc/paths.d has 10-nanobrew and 20-homebrew
	char pathsD[4096];
	snprintf(pathsD, sizeof(pathsD), "%s/etc/paths.d", prefixPath);
	mkdir(pathsD, 0755);

	char nbPathFile[4096];
	snprintf(nbPathFile, sizeof(nbPathFile), "%s/10-nanobrew", pathsD);
	if (access(nbPathFile, F_OK) != 0)
	{
		FILE* f = fopen(nbPathFile, "w");
		if (f)
		{
			fputs("/opt/nanobrew/prefix/bin\n", f);
			fclose(f);
		}
	}

	char brewPathFile[4096];
	snprintf(brewPathFile, sizeof(brewPathFile), "%s/20-homebrew", pathsD);
	if (access(brewPathFile, F_OK) != 0)
	{
		FILE* f = fopen(brewPathFile, "w");
		if (f)
		{
			fputs("/opt/homebrew/bin\n", f);
			fclose(f);
		}
	}
}

void darlingPreInit(const char* prefix)
{
	// This runs between temp_drop_privileges() and regain_privileges(), so geteuid() is
	// never 0 here; check the saved set-user-ID to tell a temporary drop from non-root mode.
	uid_t ruid, euid, suid;
	bool hasRoot = getresuid(&ruid, &euid, &suid) == 0 && (ruid == 0 || euid == 0 || suid == 0);
	if (getenv("DARLING_NONROOT") != NULL || !hasRoot)
	{
		ensureProcSymlink(prefix);
	}
	ensureShSymlink(prefix);
	ensureHomebrewSymlinks(prefix);

	// TODO: Run /usr/libexec/makewhatis
	// Clear runtime state, not /var/tmp files that applications may need
	// across separate server invocations using the same prefix.
	const char* dirs[] = {
		"/var/run"
	};

	char fullpath[4096];
	strcpy(fullpath, prefix);
	const size_t prefixLen = strlen(fullpath);

	for (size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); i++)
	{
		fullpath[prefixLen] = 0;
		strcat(fullpath, dirs[i]);
		wipeDir(fullpath);
	}
}

void spawnLaunchd(const char* prefix)
{
	puts("Bootstrapping the container with launchd...");

	// putenv("KQUEUE_DEBUG=1");

	auto tmp = (std::string(prefix) + "/.darlingserver.sock");

	const char* initPath = getenv("DSERVER_INIT");

	if (!initPath) {
		initPath = DARLINGSERVER_INIT_PROCESS;
	}

	std::string libexec = DarlingServer::Config::getLibexecPath();
	setenv("__mldr_DYLD_ROOT_PATH", libexec.c_str(), 1);
	setenv("__mldr_sockpath", tmp.c_str(), 1);
	std::string mldrPath = DarlingServer::Config::defaultMldrPath();
	char vchrootArg0[4096];
	snprintf(vchrootArg0, sizeof(vchrootArg0), "mldr!%s/usr/libexec/darling/vchroot", libexec.c_str());
	execl(mldrPath.c_str(), vchrootArg0, "vchroot", prefix, initPath, NULL);

	fprintf(stderr, "Failed to exec launchd: %s\n", strerror(errno));
	abort();
}

static bool testEnvVar(const char* var_name) {
	const char* var = getenv(var_name);

	if (!var) {
		return false;
	}

	auto len = strlen(var);

	if (len > 0 && (var[0] == '1' || var[0] == 't' || var[0] == 'T')) {
		return true;
	}

	return false;
};

static bool isOnWsl1() {
	static bool initialized = false;
	static bool result = false;
	if (!initialized) {
		initialized = true;
		// WSL1 kernels report a release such as "4.4.0-19041-Microsoft"; WSL2 uses "-microsoft-standard".
		struct utsname name;
		result = uname(&name) == 0 && strstr(name.release, "Microsoft") != NULL;
	}

	return result;
}

static bool shouldUseOverlayFs() {
	bool shouldUse = true;
	bool explicitlySet = false;

	if (testEnvVar("DARLING_NOOVERLAYFS")) {
		shouldUse = false;
		explicitlySet = true;
	} else if (getenv("DARLING_NOOVERLAYFS")) {
		shouldUse = true;
		explicitlySet = true;
	}

	// https://github.com/microsoft/WSL/issues/8748
	// Microsoft is being dumb with its overlayfs implementation and they don't seem to be willing to be fix WSL1-related bugs.
	// We therefore have to enable this hack on WSL1.
	if (!explicitlySet && isOnWsl1()) {
		shouldUse = false;
	}

	return shouldUse;
}

static int compareTimespec(const timespec& a, const timespec& b) {
	if (a.tv_sec != b.tv_sec) {
		return (a.tv_sec > b.tv_sec) ? 1 : -1;
	} else if (a.tv_nsec != b.tv_nsec) {
		return (a.tv_nsec > b.tv_nsec) ? 1 : -1;
	} else {
		return 0;
	}
}

// The copy runs with the user's privileges, and entries an earlier root-mode copy left
// root-owned can't be updated; skip them instead of aborting the whole copy.
static bool skipUnwritableEntry(const char* what, const std::string& path) {
	if (errno != EPERM && errno != EACCES) {
		return false;
	}
	fprintf(stderr, "Skipping %s: cannot %s: %s\n", path.c_str(), what, strerror(errno));
	return true;
}

static void copyAndSetAttributes(std::string& fromPath, std::string& toPath) {
	struct stat fromStat, toStat;
	if (lstat(fromPath.c_str(), &fromStat) == -1) {
		fprintf(stderr, "Failed to stat file %s: %s\n", fromPath.c_str(), strerror(errno));
		abort();
	}
	bool destinationExists = true;
	bool updateAttributes = false;
	if (lstat(toPath.c_str(), &toStat) == -1) {
		if (errno != ENOENT) {
			fprintf(stderr, "Failed to stat file %s: %s\n", toPath.c_str(), strerror(errno));
			abort();
		}
		destinationExists = false;
	}

	if (S_ISDIR(fromStat.st_mode)) {
		if (destinationExists && !S_ISDIR(toStat.st_mode)) {
			return;
		} else {
			if (!destinationExists) {
				if (mkdir(toPath.c_str(), fromStat.st_mode & ALLPERMS) == -1) {
					if (skipUnwritableEntry("create the directory", toPath)) {
						return;
					}
					fprintf(stderr, "Failed to create directory %s: %s\n", toPath.c_str(), strerror(errno));
					abort();
				}
				updateAttributes = true;
			}
			DIR* fromDir = opendir(fromPath.c_str());
			if (fromDir == NULL) {
				fprintf(stderr, "Failed to open directory %s: %s\n", fromPath.c_str(), strerror(errno));
				abort();
			}

			struct dirent* entry = NULL;
			while ((errno = 0) || ((entry = readdir(fromDir)) != NULL)) {
				if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
					continue;
				}

				size_t oldFromSize = fromPath.size();
				size_t oldToSize = toPath.size();

				fromPath.push_back('/');
				toPath.push_back('/');

				fromPath.append(entry->d_name);
				toPath.append(entry->d_name);

				copyAndSetAttributes(fromPath, toPath);

				fromPath.resize(oldFromSize);
				toPath.resize(oldToSize);
			}

			if (errno) {
				fprintf(stderr, "Failed to read directory %s: %s\n", fromPath.c_str(), strerror(errno));
				abort();
			}

			if (closedir(fromDir) == -1) {
				fprintf(stderr, "Failed to close directory %s: %s\n", fromPath.c_str(), strerror(errno));
			}
		}
	} else {
		if (destinationExists) {
			if ((fromStat.st_mode & S_IFMT) != (toStat.st_mode & S_IFMT)) {
				return;
			} else {
				int compareResult = compareTimespec(fromStat.st_mtim, toStat.st_mtim);
				// the lower file is older, don't touch the newer file.
				if (compareResult == -1) {
					return;
				// the lower file and the newer files should be the same.
				// we still update the attributes though, in case ownership or permission changes.
				} else if (compareResult == 0) {
					updateAttributes = true;
				} else if (compareResult == 1) {
					if (unlink(toPath.c_str()) == -1) {
						if (skipUnwritableEntry("replace the file", toPath)) {
							return;
						}
						fprintf(stderr, "Failed to delete old destination file %s: %s\n", toPath.c_str(), strerror(errno));
						abort();
					}
					std::error_code ec;
					std::filesystem::copy(fromPath, toPath, std::filesystem::copy_options::copy_symlinks, ec);
					if (ec) { fprintf(stderr, "copy %s -> %s failed: %s\n", fromPath.c_str(), toPath.c_str(), ec.message().c_str()); return; }
					updateAttributes = true;
				}
			}
		} else {
			std::error_code ec;
			std::filesystem::copy(fromPath, toPath, std::filesystem::copy_options::copy_symlinks, ec);
			if (ec) { fprintf(stderr, "copy %s -> %s failed: %s\n", fromPath.c_str(), toPath.c_str(), ec.message().c_str()); return; }
			updateAttributes = true;
		}
	}

	if (updateAttributes) {
		struct timespec times[] = {
			fromStat.st_atim,
			fromStat.st_mtim
		};
		if (utimensat(-1, toPath.c_str(), times, AT_SYMLINK_NOFOLLOW) == -1) {
			if (skipUnwritableEntry("set the timestamp", toPath)) {
				return;
			}
			fprintf(stderr, "Failed to set timestamp for %s: %s\n", toPath.c_str(), strerror(errno));
			abort();
    	}
		if (fchownat(-1, toPath.c_str(), fromStat.st_uid, fromStat.st_gid, AT_SYMLINK_NOFOLLOW) == -1) {
			// The copy runs as the user, who cannot hand files to root; the prefix stays owned by the user instead.
			if (errno != EPERM && errno != EINVAL) {
				fprintf(stderr, "Failed to set owner for %s: %s\n", toPath.c_str(), strerror(errno));
				abort();
			}
		}
		// POSIX said that AT_SYMLINK_NOFOLLOW is acceptable for links, but on Linux all calls with AT_SYMLINK_NOFOLLOW fails with ENOTSUP.
		if (fchmodat(-1, toPath.c_str(), fromStat.st_mode & ALLPERMS, S_ISLNK(fromStat.st_mode) ? AT_SYMLINK_NOFOLLOW : 0) == -1) {
			if (!(S_ISLNK(fromStat.st_mode) && (errno == ENOTSUP)) && !skipUnwritableEntry("set the permissions", toPath)) {
				fprintf(stderr, "Failed to set permissions for %s: %s\n", toPath.c_str(), strerror(errno));
				abort();
			}
		}
	}
}

static void temp_drop_privileges(uid_t uid, gid_t gid) {
	if (geteuid() != 0) {
		return;
	}
	// it's important to drop GID first, because non-root users can't change their GID
	if (setresgid(gid, gid, 0) < 0) {
		fprintf(stderr, "Failed to temporarily drop group privileges\n");
		exit(1);
	}
	if (setresuid(uid, uid, 0) < 0) {
		fprintf(stderr, "Failed to temporarily drop user privileges\n");
		exit(1);
	}
};

static void perma_drop_privileges(uid_t uid, gid_t gid) {
	if (geteuid() != 0) {
		return;
	}
	if (setresgid(gid, gid, gid) < 0) {
		fprintf(stderr, "Failed to drop group privileges\n");
		exit(1);
	}
	if (setresuid(uid, uid, uid) < 0) {
		fprintf(stderr, "Failed to drop user privileges\n");
		exit(1);
	}
};

static void regain_privileges() {
	// temp_drop_privileges() leaves root only in the saved set-user-ID (real and effective
	// are both the original user), so check all three: only a process that never had root
	// (non-root mode) should skip regaining it.
	uid_t ruid, euid, suid;
	if (getresuid(&ruid, &euid, &suid) == 0 && ruid != 0 && euid != 0 && suid != 0) {
		return;
	}
	if (seteuid(0) < 0) {
		fprintf(stderr, "Failed to regain root EUID\n");
		exit(1);
	}
	if (setegid(0) < 0) {
		fprintf(stderr, "Failed to regain root EGID\n");
		exit(1);
	}

	if (setresuid(0, 0, 0) < 0) {
		fprintf(stderr, "Failed to regain root privileges\n");
		exit(1);
	}
	if (setresgid(0, 0, 0) < 0) {
		fprintf(stderr, "Failed to regain root privileges\n");
		exit(1);
	}
};

// Copies as the user, so that root never writes through links inside the user's prefix.
static void copyLibexecIntoPrefix(const char* prefix, uid_t originalUID, gid_t originalGID) {
	std::string fromPath = DarlingServer::Config::getLibexecPath();
	std::string toPath = prefix;
	temp_drop_privileges(originalUID, originalGID);
	copyAndSetAttributes(fromPath, toPath);
	regain_privileges();
}

#if DSERVER_ASAN
static void handle_sigusr1(int signum) {
	__lsan_do_recoverable_leak_check();
};
#endif

int main(int argc, char** argv) {
	// An embedding launcher may already own namespaces, userland and process
	// lifetime. Keep only the real RPC/XNU service in this explicit mode.
	if (argc == 3 && strcmp(argv[1], "--rpc-only") == 0) {
		struct stat st;
		if (stat(argv[2], &st) != 0 || !S_ISDIR(st.st_mode)
			|| st.st_uid != getuid() || (st.st_mode & 077) != 0) {
			fprintf(stderr, "RPC prefix must be a private directory owned by the caller\n");
			return 1;
		}
		signal(SIGPIPE, SIG_IGN);
#if DSERVER_SINGLE_CPU
		cpu_set_t available, one;
		if (sched_getaffinity(0, sizeof(available), &available) != 0) return 1;
		CPU_ZERO(&one);
		for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
			if (CPU_ISSET(cpu, &available)) { CPU_SET(cpu, &one); break; }
		}
		if (sched_setaffinity(0, sizeof(one), &one) != 0) return 1;
#endif
		try {
			auto server = new DarlingServer::Server(argv[2]);
			fprintf(stderr, "DARLINGSERVER_RPC_ONLY prefix=%s host_page=%ld guest_shift=%d\n",
				argv[2], sysconf(_SC_PAGESIZE), DSERVER_GUEST_PAGE_SHIFT);
			server->start();
		} catch (const std::exception& e) {
			fprintf(stderr, "RPC server failed: %s\n", e.what());
		}
		return 1;
	}
	const char* prefix = NULL;
	uid_t originalUID = -1;
	gid_t originalGID = -1;
	int pipefd = -1;
	bool fix_permissions = false;
	pid_t launchdGlobalPID = -1;
	struct rlimit default_limit;
	struct rlimit increased_limit;
	FILE* nr_open_file = NULL;
	int childWaitFDs[2];
	struct rlimit core_limit;

	char putOld[4096];
	char *p;

	if (argc < 6) {
		fprintf(stderr, "darlingserver is not meant to be started manually\n");
		exit(1);
	}

#if DSERVER_EXTENDED_DEBUG
	if (getenv("DSERVER_WAIT4DEBUGGER")) {
		volatile bool debugged = false;
		while (!debugged);
	}
#endif

	prefix = argv[1];
	sscanf(argv[2], "%d", &originalUID);
	sscanf(argv[3], "%d", &originalGID);
	sscanf(argv[4], "%d", &pipefd);

	if (argv[5][0] == '1') {
		fix_permissions = true;
	}

	// Without trailing slashes, so the parent/name split works and ".workdir" is the prefix's sibling.
	std::string prefixPath = prefix;
	while (prefixPath.size() > 1 && prefixPath.back() == '/')
		prefixPath.pop_back();


	// temporarily drop privileges to perform some prefix work
	temp_drop_privileges(originalUID, originalGID);
	setupUserHome(prefix, originalUID);
	//setupCoredumpPattern();
	regain_privileges();

	// read the default rlimit so we can restore it for our children
	if (getrlimit(RLIMIT_NOFILE, &default_limit) != 0) {
		fprintf(stderr, "Failed to read default FD rlimit: %s\n", strerror(errno));
		//exit(1);
	} else {
		// ASAN uses a rather obtuse way of closing descriptors for child processes it spawns,
		// and increasing the FD limit screws with it (it tries to close every single descriptor up to the FD limit).
#if !DSERVER_ASAN
		// read the system maximum
		nr_open_file = fopen("/proc/sys/fs/nr_open", "r");
		if (nr_open_file == NULL) {
			if (geteuid() == 0 && errno != EACCES && errno != EPERM)
				fprintf(stderr, "Warning: failed to open /proc/sys/fs/nr_open: %s\n", strerror(errno));
			increased_limit.rlim_cur = increased_limit.rlim_max = default_limit.rlim_max;
			//exit(1);
		} else {
			unsigned long read_max = 0;
			if (fscanf(nr_open_file, "%lu", &read_max) == 1) {
				increased_limit.rlim_cur = increased_limit.rlim_max = read_max;
			} else {
				increased_limit.rlim_cur = increased_limit.rlim_max = default_limit.rlim_max;
			}
			if (fclose(nr_open_file) != 0) {
				fprintf(stderr, "Failed to close /proc/sys/fs/nr_open: %s\n", strerror(errno));
				exit(1);
			}
		}

		// now set our increased rlimit
		if (setrlimit(RLIMIT_NOFILE, &increased_limit) != 0) {
			fprintf(stderr, "Warning: failed to increase FD rlimit: %s\n", strerror(errno));
			//exit(1);
		}

#endif
	}

	if (getrlimit(RLIMIT_CORE, &core_limit) != 0) {
		fprintf(stderr, "Failed to read default core rlimit: %s\n", strerror(errno));
	} else {
		// increase the core limit to the maximum
		core_limit.rlim_cur = core_limit.rlim_max;

		if (setrlimit(RLIMIT_CORE, &core_limit) != 0) {
			fprintf(stderr, "Warning: failed to increase core limit: %s\n", strerror(errno));
		}
	}

	// Setup mount namespace if root
	if (geteuid() == 0 && unshare(CLONE_NEWNS) != 0)
	{
		fprintf(stderr, "Cannot unshare PID and mount namespaces: %s\n", strerror(errno));
		exit(1);
	}

	// systemd marks / as MS_SHARED and the new namespace inherits that, so without this the
	// /dev/shm tmpfs and overlay mounts below would propagate back into the host's namespace.
	if (geteuid() == 0 && mount(NULL, "/", NULL, MS_REC | MS_SLAVE, NULL) != 0)
	{
		fprintf(stderr, "Cannot remount / as slave: %s\n", strerror(errno));
		exit(1);
	}

	int shmMountFlags = MS_NOSUID | MS_NODEV;
	// Workaround for dumb Microsoft bug: https://github.com/microsoft/WSL/issues/8777
	if (!isOnWsl1())
	{
		shmMountFlags |= MS_NOEXEC;
	}

	if (geteuid() == 0)
	{
		umount("/dev/shm");
		if (mount("tmpfs", "/dev/shm", "tmpfs", shmMountFlags, NULL) != 0)
		{
			fprintf(stderr, "Cannot mount new /dev/shm: %s\n", strerror(errno));
			exit(1);
		}
	}

	if (shouldUseOverlayFs() && geteuid() == 0) {
		// Overlayfs writes into the upper and work directories as root, so mount through
		// descriptors checked to be the user's own directories, not through their paths.
		// Both come from one parent descriptor, so the work directory really is the prefix's sibling.
		size_t slash = prefixPath.rfind('/');
		std::string parentPath = slash == std::string::npos ? "." : (slash == 0 ? "/" : prefixPath.substr(0, slash));
		std::string upperName = prefixPath.substr(slash + 1);
		temp_drop_privileges(originalUID, originalGID);
		int parentFD = open(parentPath.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
		if (parentFD < 0) {
			fprintf(stderr, "Cannot open directory %s: %s\n", parentPath.c_str(), strerror(errno));
			exit(1);
		}
		regain_privileges();
		int upperFD = openPrefixDir(parentFD, upperName.c_str(), O_NOFOLLOW, false, originalUID, originalGID);
		// The launcher creates .workdir, so it is never a symlink; launchers before the
		// user-owned prefix changes created it as root.
		int workFD = openPrefixDir(parentFD, (upperName + ".workdir").c_str(), O_NOFOLLOW, true, originalUID, originalGID);
		close(parentFD);
		std::string libexec = DarlingServer::Config::getLibexecPath();

		// On prefix creation, give the user their own copies of the lower directories.
		if (fix_permissions) {
			temp_drop_privileges(originalUID, originalGID);
			int lowerFD = open(libexec.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
			if (lowerFD < 0) {
				fprintf(stderr, "Cannot open directory %s: %s\n", libexec.c_str(), strerror(errno));
				exit(1);
			}
			mirrorDirectoryTree(lowerFD, upperFD);
			regain_privileges();
		}

		// Overlayfs empties an existing work/ in its workdir as root, so give it a directory only root can write to.
		const char overlayWorkName[] = "darlingserver";
		if (mkdirat(workFD, overlayWorkName, 0700) != 0 && errno != EEXIST) {
			fprintf(stderr, "Cannot create %s.workdir/%s: %s\n", upperName.c_str(), overlayWorkName, strerror(errno));
			exit(1);
		}
		int overlayWorkFD = openat(workFD, overlayWorkName, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		struct stat overlayWorkStat;
		if (overlayWorkFD < 0 || fstat(overlayWorkFD, &overlayWorkStat) != 0
			|| overlayWorkStat.st_uid != 0 || (overlayWorkStat.st_mode & 077) != 0)
		{
			fprintf(stderr, "%s.workdir/%s is not a private root directory\n", upperName.c_str(), overlayWorkName);
			exit(1);
		}
		close(workFD);

		std::string target = "/proc/self/fd/" + std::to_string(upperFD);
		std::string opts = "lowerdir=" + libexec
			+ ",upperdir=" + target
			+ ",workdir=/proc/self/fd/" + std::to_string(overlayWorkFD);

		if (mount("overlay", target.c_str(), "overlay", 0, (opts + ",index=off").c_str()) != 0
			&& (errno != EINVAL || mount("overlay", target.c_str(), "overlay", 0, opts.c_str()) != 0))
		{
			fprintf(stderr, "Cannot mount overlay: %s; falling back to direct copy\n", strerror(errno));
			copyLibexecIntoPrefix((prefixPath + "/").c_str(), originalUID, originalGID);
		}

		close(upperFD);
		close(overlayWorkFD);
	} else {
		copyLibexecIntoPrefix((prefixPath + "/").c_str(), originalUID, originalGID);
	}

	// temporarily drop privileges and do some prefix work
	temp_drop_privileges(originalUID, originalGID);
	darlingPreInit(prefix);
	regain_privileges();

	// Tell the parent we're ready
	write(pipefd, ".", 1);
	close(pipefd);

	// The child reports that its namespace is set up through [0]; the parent gives it the green light through [1].
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, childWaitFDs) != 0) {
		std::cerr << "Failed to create child waiting socket pair: " << strerror(errno) << std::endl;
		exit(1);
	}

	// we have to use `clone` rather than `fork` to create the process in its own PID namespace
	// and still be able to spawn new processes and threads of our own
	launchdGlobalPID = syscall(SYS_clone, CLONE_NEWPID | SIGCHLD, NULL, NULL, NULL, 0);

	if (launchdGlobalPID < 0) {
		launchdGlobalPID = fork();
	}

	if (launchdGlobalPID < 0) {
		fprintf(stderr, "Failed to fork to start launchd: %s\n", strerror(errno));
		exit(1);
	} else if (launchdGlobalPID == 0) {
		// this is the child
		char buf[1];

		close(childWaitFDs[1]);

		// Detach launchd into its own session + process group, so that any
		// signals it (or its children) broadcast via kill(0/-1, ...) or
		// killpg(0, ...) do not propagate back up to darlingserver / darling /
		// timeout (which all share the same pgrp by default). On ARM64 we
		// observed launchd's startup broadcasting SIGTRAP, killing the parents.
		if (setsid() == (pid_t)-1) {
			if (errno != EPERM)
				fprintf(stderr, "Warning: setsid() failed before launchd: %s\n", strerror(errno));
			// continue anyway
		}

		if (geteuid() == 0)
		{
			// mount procfs for our new PID namespace, through the user's prefix rather than its path
			int prefixFD = openPrefixDir(AT_FDCWD, prefixPath.c_str(), O_NOFOLLOW, false, originalUID, originalGID);
			int procFD = openat(prefixFD, "proc", O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			snprintf(putOld, sizeof(putOld), "/proc/self/fd/%d", procFD);
			if (procFD < 0 || mount("proc", putOld, "proc", 0, "") != 0)
			{
				fprintf(stderr, "Cannot mount procfs on %s/proc, which must be a directory: %s\n", prefix, strerror(errno));
				exit(1);
			}
			if (procFD >= 0)
				close(procFD);
			close(prefixFD);
		}
		else
		{
			ensureProcSymlink(prefix);
		}
		write(childWaitFDs[0], ".", 1);

		// drop our privileges now
		perma_drop_privileges(originalUID, originalGID);
		prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);

		// decrease the FD limit back to the default
		if (setrlimit(RLIMIT_NOFILE, &default_limit) != 0) {
			fprintf(stderr, "Warning: failed to decrease FD limit back down for launchd: %s\n", strerror(errno));
			//exit(1);
		}

		// wait for the parent to give us the green light
		read(childWaitFDs[0], buf, 1);
		close(childWaitFDs[0]);

		spawnLaunchd(prefix);
		__builtin_unreachable();
	}

	// this is the parent
	close(childWaitFDs[0]);
	char childStatus;
	if (read(childWaitFDs[1], &childStatus, 1) != 1) {
		fprintf(stderr, "launchd's process failed to set up its namespace\n");
		exit(1);
	}

	// drop our privileges
	perma_drop_privileges(originalUID, originalGID);
	prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);

	// Ignore SIGPIPE so closed sockets/pipes return EPIPE instead of killing the server
	signal(SIGPIPE, SIG_IGN);

#if DSERVER_ASAN
	// set up a signal handler to print leak info
	struct sigaction leak_info_action;
	leak_info_action.sa_handler = handle_sigusr1;
	leak_info_action.sa_flags = SA_RESTART;
	sigemptyset(&leak_info_action.sa_mask);
	sigaction(SIGUSR1, &leak_info_action, NULL);
#endif

	// create the server
	auto server = new DarlingServer::Server(prefix);

	// tell the child to go ahead; the socket has been created
	write(childWaitFDs[1], ".", 1);
	close(childWaitFDs[1]);

	// start the main loop
	try {
		server->start();
	} catch (const std::exception& e) {
		std::cerr << "=== DARLINGSERVER CRASHED WITH EXCEPTION: " << e.what() << " ===" << std::endl;
	} catch (...) {
		std::cerr << "=== DARLINGSERVER CRASHED WITH UNKNOWN EXCEPTION ===" << std::endl;
	}

	// this should never happen
	std::cerr << "Server exited main loop!" << std::endl;
	delete server;

	return 1;
};
