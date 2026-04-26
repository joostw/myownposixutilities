/*
 * echo — write arguments to standard output  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis: echo [string...]
 *
 * No options are recognized; all arguments (including leading '-n'/'-e'/'-E')
 * are treated as literal strings to be written (XSI rule).
 * XSI backslash escape sequences are processed in every argument.
 * \c suppresses the trailing newline and ignores everything that follows.
 */

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Write s to stdout with XSI backslash escape processing.
 * Returns false if \c was encountered (caller should stop and skip newline).
 */
static int write_arg(const char *s)
{
    while (*s) {
        if (*s != '\\') {
            putchar_unlocked(*s++);
            continue;
        }
        s++;
        switch (*s) {
        case 'a':  putchar_unlocked('\a'); s++; break;
        case 'b':  putchar_unlocked('\b'); s++; break;
        case 'c':  return 0;               /* suppress newline, stop */
        case 'f':  putchar_unlocked('\f'); s++; break;
        case 'n':  putchar_unlocked('\n'); s++; break;
        case 'r':  putchar_unlocked('\r'); s++; break;
        case 't':  putchar_unlocked('\t'); s++; break;
        case 'v':  putchar_unlocked('\v'); s++; break;
        case '\\': putchar_unlocked('\\'); s++; break;
        case '0': {
            s++;
            unsigned char val = 0;
            int digits = 0;
            while (digits < 3 && *s >= '0' && *s <= '7') {
                val = (unsigned char)(val * 8 + (*s - '0'));
                s++;
                digits++;
            }
            putchar_unlocked(val);
            break;
        }
        default:
            /* Unrecognized escape: pass through backslash literally */
            putchar_unlocked('\\');
            break;
        }
    }
    return 1;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    for (int i = 1; i < argc; i++) {
        if (i > 1) putchar_unlocked(' ');
        if (!write_arg(argv[i])) goto done;
    }
    putchar_unlocked('\n');
done:
    if (fflush(stdout) != 0 || ferror(stdout)) return 1;
    return 0;
}
