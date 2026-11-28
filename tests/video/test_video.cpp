// Tests of the small pieces of Vid3os (src/video/util.h).
#include <cstdio>

#include "../../src/video/util.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main() {
    CHECK(vid::formatTime(0) == "0:00");
    CHECK(vid::formatTime(65) == "1:05");
    CHECK(vid::formatTime(3723.9) == "1:02:03");
    CHECK(vid::formatTime(-4) == "0:00");
    CHECK(vid::formatTime(NAN) == "0:00");
    CHECK(vid::nextSpeed(1.0) == 1.25);
    CHECK(vid::nextSpeed(2.0) == 0.5);
    CHECK(vid::nextSpeed(3.3) == 1.0);
    CHECK(vid::speedLabel(1.0) == "1×");
    CHECK(vid::speedLabel(1.5) == "1.5×");
    CHECK(vid::speedLabel(0.75) == "0.75×");
    CHECK(vid::barFraction(50, 0, 100) == 0.5);
    CHECK(vid::barFraction(-10, 0, 100) == 0);
    CHECK(vid::barFraction(500, 0, 100) == 1);
    CHECK(vid::barFraction(5, 0, 0) == 0);
    if (failures == 0) std::puts("video util: all ok");
    return failures ? 1 : 0;
}
