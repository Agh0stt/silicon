#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct { int8_t a; float b; uint16_t c; double d; bool e; int64_t f; uint8_t g[3]; void *h; } Mixed;
uint64_t mixed_size(void); int64_t offset_of(void *, int); double sum_fields(Mixed *); void fill(Mixed *);
int main(void) {
    Mixed m = { .b = 1.5f, .d = 2.25 }; int bad = 0;
    size_t want[8] = { offsetof(Mixed,a), offsetof(Mixed,b), offsetof(Mixed,c), offsetof(Mixed,d), offsetof(Mixed,e), offsetof(Mixed,f), offsetof(Mixed,g), offsetof(Mixed,h) };
    if (mixed_size() != sizeof(Mixed)) { printf("FAIL size %lu vs %zu\n", (unsigned long)mixed_size(), sizeof(Mixed)); bad++; }
    for (int i = 0; i < 8; i++) if ((size_t)offset_of(&m, i) != want[i]) { printf("FAIL offset %d: %ld vs %zu\n", i, (long)offset_of(&m, i), want[i]); bad++; }
    if (sum_fields(&m) != 3.75) { puts("FAIL sum_fields"); bad++; }
    fill(&m);
    if (m.a != -7 || m.c != 65000 || m.e != true || m.f != -123456789012LL) { puts("FAIL fill"); bad++; }
    puts(bad ? "struct layout: FAILED" : "struct layout: matches C"); return bad;
}
