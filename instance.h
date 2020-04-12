//
// Created by jaakko on 9.4.2020.
//

#ifndef WM_INSTANCE_H
#define WM_INSTANCE_H

#include <X11/Xlib.h>

typedef struct wmWindow wmWindow;
struct wmWindow {
    wmWindow* next;
    wmWindow* previous;
    Window window;
    Window frame;
};

wmWindow* wmActiveWindow;
wmWindow* wmHead;
wmWindow* wmTail;

int wmRunning;
int wmExitCode;

Cursor wmCursor;

Display* wmDisplay;
Window wmRoot;
int wmDepth;
Visual* wmVisual;
Colormap wmColormap;
int wmScreenWidth;
int wmScreenHeight;

int wmInitialize();
void wmRun();
void wmFree();

void wmFocusWindow(wmWindow* window);
void wmRequestCloseWindow(wmWindow* window);

void wmNewWindow(Window window, const XWindowAttributes* attributes);
void wmFreeWindow(wmWindow* window);
wmWindow* wmWindowTowmWindow(Window window);

#endif //WM_INSTANCE_H
