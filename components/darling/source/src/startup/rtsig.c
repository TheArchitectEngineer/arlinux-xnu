#include <stdlib.h>
#include <signal.h>
#include <stdio.h>

// Determine RT signal ranges without polluting the build environment with Linux system headers
int main(int argc, char** argv)
{
	FILE* output = stdout;

	if (argc > 1)
	{
		output = fopen(argv[1], "w");
		if (!output)
		{
			perror("fopen");
			return 1;
		}
	}

	/* Android Bionic libc reserves signals 32..42; guest RT signals begin at 43 by default.
	 * Allow override via DARLING_SIGRTMIN environment variable if host Bionic changes range. */
	const char* env_rtmin = getenv("DARLING_SIGRTMIN");
	int rtmin = (env_rtmin && env_rtmin[0]) ? atoi(env_rtmin) : 43;
	fprintf(output, "#define LINUX_SIGRTMIN %d\n", rtmin);
	fprintf(output, "#define LINUX_SIGRTMAX %d\n", SIGRTMAX);

	fclose(output);
	return 0;
}

