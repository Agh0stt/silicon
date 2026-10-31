#include <stdio.h>
#include <string.h>
extern const int primes[3]; extern const char *first; extern int mutable; int get_prime(int);
int main(void) {
    int bad = 0;
    if (primes[2] != 5 || get_prime(1) != 3) { puts("FAIL primes"); bad++; }
    if (strcmp(first, "alpha")) { puts("FAIL first"); bad++; }
    mutable = 5; if (mutable != 5) bad++;
    puts(bad ? "ro: FAILED" : "ro: ok"); return bad;
}
