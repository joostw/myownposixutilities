/*
 * basename — return non-directory portion of a pathname  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis: basename string [suffix]
 *
 * Algorithm (steps per spec):
 *  1. Empty string → '.' (implementation choice; spec allows '.' or empty)
 *  2. "//" → implementation-defined; process normally (step 3 applies)
 *  3. All slashes → output '/'
 *  4. Strip trailing slashes
 *  5. Strip directory prefix up to and including last '/'
 *  6. Strip suffix if it matches end but not the whole remaining string
 */

#include <locale.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: basename string [suffix]\n");
        return 1;
    }

    /* Step 1: empty string */
    if (argv[1][0] == '\0') { puts("."); return 0; }

    char *s = argv[1];
    size_t n = strlen(s);

    /* Step 3: entirely slashes */
    size_t nslash = 0;
    while (nslash < n && s[nslash] == '/') nslash++;
    if (nslash == n) { puts("/"); return 0; }

    /* Step 4: strip trailing slashes */
    while (n > 0 && s[n - 1] == '/') n--;
    s[n] = '\0';

    /* Step 5: strip directory prefix */
    char *last = strrchr(s, '/');
    if (last) s = last + 1;

    /* Step 6: strip suffix (if given, not equal to whole string, matches end) */
    if (argc == 3) {
        const char *suffix = argv[2];
        size_t slen = strlen(s);
        size_t suflen = strlen(suffix);
        if (suflen > 0 && suflen < slen &&
            strcmp(s + slen - suflen, suffix) == 0)
            s[slen - suflen] = '\0';
    }

    puts(s);
    return 0;
}
