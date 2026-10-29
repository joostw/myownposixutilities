/*
 * fstat.c - write file status for the tests: fstat FORMAT file...
 *
 * POSIX has no utility that shows a file's status exactly (ls -l shows
 * minutes), so the tests compile this.  It writes FORMAT for each file,
 * followed by a newline, with the conversions of GNU stat -c that the
 * tests use, in the same form:
 *   %n name           %s size          %h number of links
 *   %a permission and mode bits in octal    %i file serial number
 *   %b blocks (512-byte units, st_blocks)   %u %g user and group ID
 *   %d the device (st_dev, in decimal)
 *   %Y %X modification and access time in seconds since the Epoch
 *   %y %x those times as YYYY-MM-DD hh:mm:ss.nnnnnnnnn +zzzz (local time)
 *   %% a percent sign
 * Symbolic links are not followed (lstat).  Exit status 1 if a file
 * cannot be examined.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static void when(struct timespec t)
{
    char buf[64];
    struct tm *tm = localtime(&t.tv_sec);
    if (!tm) {
        printf("?");
        return;
    }
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", tm);
    printf("%s.%09ld", buf, (long)t.tv_nsec);
    strftime(buf, sizeof buf, " %z", tm);
    printf("%s", buf);
}

int main(int argc, char *argv[])
{
    int status = 0;
    if (argc < 3) {
        fprintf(stderr, "usage: fstat format file...\n");
        return 2;
    }
    for (int i = 2; i < argc; i++) {
        struct stat st;
        if (lstat(argv[i], &st) != 0) {
            perror(argv[i]);
            status = 1;
            continue;
        }
        for (const char *f = argv[1]; *f; f++) {
            if (*f != '%' || !f[1]) {
                putchar(*f);
                continue;
            }
            switch (*++f) {
            case 'n': printf("%s", argv[i]); break;
            case 's': printf("%lld", (long long)st.st_size); break;
            case 'h': printf("%lu", (unsigned long)st.st_nlink); break;
            case 'a': printf("%lo", (unsigned long)(st.st_mode & 07777)); break;
            case 'i': printf("%llu", (unsigned long long)st.st_ino); break;
            case 'd': printf("%llu", (unsigned long long)st.st_dev); break;
            case 'b': printf("%lld", (long long)st.st_blocks); break;
            case 'u': printf("%lu", (unsigned long)st.st_uid); break;
            case 'g': printf("%lu", (unsigned long)st.st_gid); break;
            case 'Y': printf("%lld", (long long)st.st_mtim.tv_sec); break;
            case 'X': printf("%lld", (long long)st.st_atim.tv_sec); break;
            case 'y': when(st.st_mtim); break;
            case 'x': when(st.st_atim); break;
            case '%': putchar('%'); break;
            default: putchar('%'); putchar(*f); break;
            }
        }
        putchar('\n');
    }
    return status;
}
