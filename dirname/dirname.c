/*
 * dirname — return the directory portion of a pathname  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis: dirname string
 *
 * Algorithm:
 *  1. Empty string → '.'
 *  2. Strip trailing slashes that are not also leading slashes
 *  3. No '/' left → '.'
 *  4. Strip last component (everything after the last '/')
 *  5. Strip trailing slashes from result
 *  6. Empty result → '/'
 */

#include <locale.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: dirname string\n");
        return 1;
    }

    /* Step 1: empty string */
    if (argv[1][0] == '\0') { puts("."); return 0; }

    char *s = argv[1];
    size_t n = strlen(s);

    /* Step 2: strip trailing slashes that are not leading slashes.
     * A slash at position i is a "leading slash" only if all of s[0..i] are slashes.
     * In practice: strip trailing slashes while keeping at least the leading ones. */
    /* Find first non-slash to know where leading slashes end */
    size_t lead = 0;
    while (lead < n && s[lead] == '/') lead++;

    /* Strip trailing slashes down to max(lead, 1) */
    while (n > 1 && s[n - 1] == '/' && n > lead) n--;
    s[n] = '\0';

    /* Step 3: find last slash */
    char *last = strrchr(s, '/');
    if (!last) { puts("."); return 0; }

    /* Step 4: strip last component */
    *last = '\0';
    n = (size_t)(last - s);

    /* Step 5: strip trailing slashes from result, down to 1 */
    while (n > 1 && s[n - 1] == '/') s[--n] = '\0';

    /* Step 6: empty result means root */
    puts(n == 0 ? "/" : s);
    return 0;
}
