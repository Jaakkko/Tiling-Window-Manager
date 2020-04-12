#include "instance.h"

#include <stdlib.h>

int main() {
    if (!wmInitialize()) {
        exit(EXIT_FAILURE);
    }

    wmRun();
    wmFree();

    exit(wmExitCode);
}
