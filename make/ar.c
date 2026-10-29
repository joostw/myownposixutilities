/*
 * ar.c - members of archive libraries: lib(member.o).  Reads the
 * common ar format with GNU/SVR4 (/ and // tables) and BSD (#1/len)
 * long names.
 */
#include "make.h"

int parse_lib_member(const char *name, char **lib, char **member)
{
    const char *lp = strchr(name, '(');
    size_t l = strlen(name);
    *lib = *member = NULL;
    if (!lp || lp == name || l < 3 || name[l - 1] != ')' || lp + 1 >= name + l - 1)
        return 0;
    *lib = xstrndup(name, (size_t)(lp - name));
    *member = xstrndup(lp + 1, l - (size_t)(lp - name) - 2);
    return 1;
}

static long field_num(const char *p, int n)
{
    char b[24];
    memcpy(b, p, (size_t)n);
    b[n] = 0;
    return strtol(b, NULL, 10);
}

/* find the member; returns its header offset or -1 */
static long find_member(FILE *fp, const char *member, long *date)
{
    char magic[8], h[60];
    char *longnames = NULL;
    long lnlen = 0;
    const char *base = strrchr(member, '/');
    base = base ? base + 1 : member;
    if (fread(magic, 1, 8, fp) != 8 || memcmp(magic, "!<arch>\n", 8) != 0)
        return -1;
    for (;;) {
        long off = ftell(fp), size;
        char name[256];
        size_t nl;
        if (fread(h, 1, 60, fp) != 60 || h[58] != '`' || h[59] != '\n')
            break;
        size = field_num(h + 48, 10);
        if (size < 0)
            break;
        memcpy(name, h, 16);
        name[16] = 0;
        nl = 16;
        while (nl && name[nl - 1] == ' ')
            name[--nl] = 0;
        if (strcmp(name, "//") == 0) {
            free(longnames);
            longnames = xmalloc((size_t)size + 1);
            if (fread(longnames, 1, (size_t)size, fp) != (size_t)size)
                break;
            longnames[size] = 0;
            lnlen = size;
            if (size % 2)
                fgetc(fp);
            continue;
        }
        if (strncmp(name, "#1/", 3) == 0) {
            long k = strtol(name + 3, NULL, 10);
            if (k <= 0 || k > 255)
                break;
            if (fread(name, 1, (size_t)k, fp) != (size_t)k)
                break;
            name[k] = 0;
            nl = strnlen(name, (size_t)k);
            name[nl] = 0;
            size -= k;
        } else if (name[0] == '/' && isdigit((unsigned char)name[1]) && longnames) {
            long k = strtol(name + 1, NULL, 10), e = k;
            if (k < 0 || k >= lnlen)
                break;
            while (e < lnlen && longnames[e] != '\n')
                e++;
            if (e > k && longnames[e - 1] == '/')
                e--;
            nl = (size_t)(e - k) < sizeof name ? (size_t)(e - k) : sizeof name - 1;
            memcpy(name, longnames + k, nl);
            name[nl] = 0;
        } else if (nl && name[nl - 1] == '/' && nl > 1)
            name[--nl] = 0;
        if (strcmp(name, base) == 0) {
            *date = field_num(h + 16, 12);
            free(longnames);
            return off;
        }
        if (fseek(fp, size + (size % 2), SEEK_CUR) != 0)
            break;
    }
    free(longnames);
    return -1;
}

int ar_member_time(const char *lib, const char *member, struct timespec *t)
{
    FILE *fp = fopen(lib, "r");
    long date, off;
    if (!fp)
        return -1;
    off = find_member(fp, member, &date);
    fclose(fp);
    if (off < 0)
        return -1;
    t->tv_sec = (time_t)date;
    t->tv_nsec = 0;
    return 0;
}

int ar_touch_member(const char *lib, const char *member)
{
    FILE *fp = fopen(lib, "r+");
    long date, off;
    char d[13];
    if (!fp)
        return -1;
    off = find_member(fp, member, &date);
    if (off < 0) {
        fclose(fp);
        return -1;
    }
    snprintf(d, sizeof d, "%-12lld", (long long)time(NULL));
    fseek(fp, off + 16, SEEK_SET);
    fwrite(d, 1, 12, fp);
    fclose(fp);
    return 0;
}
