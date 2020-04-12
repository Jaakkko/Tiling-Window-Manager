//
// Created by jaakko on 10.4.2020.
//

#include <X11/Xlib.h>

#include <stdlib.h>
#include <unistd.h>

#include "api.h"
#include "instance.h"
#include "logger.h"

void closeActiveWindow(Arg a) {
    if (wmActiveWindow) {
        wmRequestCloseWindow(wmActiveWindow);
    }
}

void focus(Arg a) {
    if (wmActiveWindow) {
        wmWindow* focus;
        if (a.i) {
            focus = wmActiveWindow->previous ? wmActiveWindow->previous : wmTail;
        }
        else {
            focus = wmActiveWindow->next ? wmActiveWindow->next : wmHead;
        }
        wmFocusWindow(focus);
    }
}

void openApplication(Arg a) {
    if (fork() == 0) {
        setsid();
        char* cmd[] = { a.v, NULL };
        execvp(cmd[0], cmd);
        exit(EXIT_SUCCESS);
    }
}

void quit(Arg a) {
    wmRunning = False;
    wmExitCode = a.i;
}
