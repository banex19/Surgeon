#include <iostream>

extern "C" {
    __attribute__((weak)) int saveCheckpoint() { return 0; }
    __attribute__((weak)) int restoreCheckpoint() { return 0; }

    int fakeFunction(int x) {
        return (x) * 2;
    }
}