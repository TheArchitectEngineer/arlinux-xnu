#undef NDEBUG
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static void check_directory_metadata(const char *path, unsigned expectedMount)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    assert(fd >= 0);
    for (unsigned byfd = 0; byfd < 2; ++byfd) {
        unsigned char buffer[128];
        struct attrlist attrs = { .bitmapcount = ATTR_BIT_MAP_COUNT,
            .commonattr = ATTR_CMN_RETURNED_ATTRS, .dirattr = ATTR_DIR_MOUNTSTATUS };
        int result = byfd ? fgetattrlist(fd, &attrs, buffer, sizeof(buffer), 0) :
            getattrlist(path, &attrs, buffer, sizeof(buffer), 0);
        uint32_t length, mask, status;
        assert(result == 0);
        memcpy(&length, buffer, 4); memcpy(&mask, buffer + 12, 4);
        memcpy(&status, buffer + 24, 4);
        printf("CHECK mount path=%s fd=%u mask=%x status=%u\n", path, byfd, mask, status);
        assert(length == 28 && mask == ATTR_DIR_MOUNTSTATUS && status == expectedMount);
        attrs.dirattr = 0;
        attrs.volattr = ATTR_VOL_INFO | ATTR_VOL_ATTRIBUTES;
        result = byfd ? fgetattrlist(fd, &attrs, buffer, sizeof(buffer), 0) :
            getattrlist(path, &attrs, buffer, sizeof(buffer), 0);
        assert(result == 0);
        uint32_t support[10];
        memcpy(&length, buffer, 4); memcpy(support, buffer + 24, sizeof(support));
        assert(length == 64);
        for (unsigned i = 0; i < 5; ++i) assert((support[i+5] & ~support[i]) == 0);
        assert((support[2] & ATTR_DIR_MOUNTSTATUS) != 0);
        assert((support[0] & 0x40000000) == 0 && (support[3] & 0x2000) == 0);
        assert((support[4] & 0x200) == 0);
        attrs.commonattr |= ATTR_CMN_FNDRINFO;
        attrs.volattr = ATTR_VOL_INFO | ATTR_VOL_CAPABILITIES;
        result = byfd ? fgetattrlist(fd, &attrs, buffer, sizeof(buffer), 0) :
            getattrlist(path, &attrs, buffer, sizeof(buffer), 0);
        assert(result == 0);
        memcpy(&length, buffer, 4);
        assert(length == 88);
        for (unsigned i = 24; i < 56; ++i) assert(buffer[i] == 0x42);
    }
    close(fd);
}

int main(void)
{
    setbuf(stdout, NULL);
    alarm(10);
    /* The isolated runner supplies a writable guest root, but no /tmp. */
    char path[] = "/bulk-guest-XXXXXX";
    assert(mkdtemp(path));
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    assert(fd >= 0);
    unsigned char rootInfo[32], childInfo[32];
    memset(rootInfo, 0x42, sizeof(rootInfo));
    memset(childInfo, 0x17, sizeof(childInfo));
    assert(setxattr("/", "com.apple.FinderInfo", rootInfo, sizeof(rootInfo), 0, 0) == 0);
    assert(setxattr(path, "com.apple.FinderInfo", childInfo, sizeof(childInfo), 0, 0) == 0);
    check_directory_metadata("/", 1);
    check_directory_metadata(path, 0);
    assert(removexattr("/", "com.apple.FinderInfo", 0) == 0);
    int file = openat(fd, "file", O_CREAT | O_WRONLY, 0600);
    assert(file >= 0 && write(file, "abc", 3) == 3);
    close(file);
    assert(mkdirat(fd, "directory", 0700) == 0);
    assert(symlinkat("missing", fd, "link") == 0);
    struct attrlist attrs = { .bitmapcount = ATTR_BIT_MAP_COUNT,
        .commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME | ATTR_CMN_OBJTYPE };
    unsigned seen = 0, calls = 0;
    for (;;) {
        unsigned char buffer[64];
        int count = getattrlistbulk(fd, &attrs, buffer, sizeof(buffer), 0);
        printf("CHECK bulk count=%d seen=%u\n", count, seen);
        assert(count >= 0 && ++calls < 10);
        if (!count) break;
        size_t cursor = 0;
        for (int i = 0; i < count; ++i) {
            uint32_t length, common, type, nameLength;
            int32_t nameOffset;
            assert(cursor + 36 <= sizeof(buffer));
            unsigned char *record = buffer + cursor;
            memcpy(&length, record, 4);
            memcpy(&common, record + 4, 4);
            memcpy(&nameOffset, record + 24, 4);
            memcpy(&nameLength, record + 28, 4);
            memcpy(&type, record + 32, 4);
            assert(length >= 36 && length <= sizeof(buffer) - cursor);
            assert((common & attrs.commonattr) == attrs.commonattr);
            assert(nameOffset >= 12 && nameLength > 0);
            assert((uint64_t)24 + nameOffset + nameLength <= length);
            const char *name = (const char *)record + 24 + nameOffset;
            assert(name[nameLength - 1] == 0);
            unsigned bit = 0;
            if (!strcmp(name, "file")) { bit = 1; assert(type == 1); }
            if (!strcmp(name, "directory")) { bit = 2; assert(type == 2); }
            if (!strcmp(name, "link")) { bit = 4; assert(type == 5); }
            assert(bit && !(seen & bit));
            seen |= bit;
            cursor += length;
        }
    }
    assert(seen == 7 && calls >= 4);
    assert(unlinkat(fd, "file", 0) == 0);
    assert(unlinkat(fd, "directory", AT_REMOVEDIR) == 0);
    assert(unlinkat(fd, "link", 0) == 0);
    close(fd);
    assert(rmdir(path) == 0);
    alarm(0);
    puts("PASS guest bulk enumeration: retries, names, types and dangling symlink");
}
