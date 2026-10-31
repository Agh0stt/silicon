#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
int32_t say(void);
double hyp(double, double);
float root32(float);
int64_t mixed(int8_t, double, uint16_t, float, int32_t, int64_t, int64_t, int64_t, int64_t, int64_t,
              double, double, double, double, double, double, double, double);
int64_t length(void);
int32_t absval(int32_t);
int64_t bump(void);
extern int64_t counter;
extern int32_t table[4];
void die_with(int32_t);
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)
int main(void) {
    int bad = 0;
    CHECK(say() == 0);
    CHECK(hyp(3.0, 4.0) == 5.0);
    CHECK(root32(2.25f) == 1.5f);
    CHECK(mixed(-3, 1.5, 65535, 2.5f, -100000, 7, 0, 0, 0, 1000, 1,2,3,4,5,6,7,8) == -3 + 65535 - 100000 + 7 + 1000);
    CHECK(length() == 27);
    CHECK(absval(-17) == 17);
    CHECK(counter == 5);
    CHECK(bump() == 5 + 99);
    CHECK(counter == 6);
    counter = 100; CHECK(bump() == 199);
    CHECK(table[2] == 30);
    printf(bad ? "interop: %d FAILED\n" : "interop: all ok\n", bad);
    if (bad) return 1;
    fflush(stdout);
    die_with(7);
    return 1;
}
