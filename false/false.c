/*
 * false.c - false: return false value (POSIX.1-2024, XCU false)
 *
 * Exit status: always a value in [1, 125] (non-zero).
 * No options, no operands, no I/O.
 */

#define _POSIX_C_SOURCE 200809L

int main(void)
{
    return 1;
}
