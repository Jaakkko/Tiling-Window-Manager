//
// Created by jaakko on 9.4.2020.
//

#ifndef WM_INSTANCE_H
#define WM_INSTANCE_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>

unsigned wmActiveWorkspace;

typedef struct wmWindow wmWindow;
struct wmWindow {
    wmWindow* next;
    wmWindow* previous;
    Window window;
    Window frame;
    unsigned workspaces;
};
wmWindow* wmHead;
wmWindow* wmTail;

typedef struct wmNode wmNode;
struct wmNode {
    unsigned numChildren;
    wmNode* nodes;
    wmWindow* window;
};
typedef struct {
    wmWindow* activeWindow;
    wmNode* layout;
} wmWorkspace;
extern wmWorkspace wmWorkspaces[];

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

Atom
    WM_PROTOCOLS,
    WM_DELETE_WINDOW,
    _NET_SUPPORTED,
    _NET_CLIENT_LIST,
    _NET_SUPPORTING_WM_CHECK,
    _NET_WM_NAME,
    _NET_WM_STATE,
    _NET_WM_STATE_FULLSCREEN;

typedef struct {
    Atom* atom;
    void (*handler)(XClientMessageEvent*);
} ClientMessageHandler;
const unsigned clientMessageHandlersCount;
extern ClientMessageHandler clientMessageHandler[];

int wmInitialize();
void wmRun();
void wmFree();

wmWindow* wmNextVisibleWindow(unsigned workspace);
wmWindow* wmPreviousVisibleWindow(unsigned workspace);
void wmMoveActiveWindow(unsigned workspace);
void wmToggleActiveWindow(unsigned workspaceIndex);

void wmFocusWindow(wmWindow* window);
void wmRequestCloseWindow(wmWindow* window);

void wmNewWindow(Window window, const XWindowAttributes* attributes);
void wmFreeWindow(wmWindow* window);
wmWindow* wmWindowTowmWindow(Window window);

void wmSelectWorkspace(unsigned workspace);
void wmShowActiveWorkspace();

#endif //WM_INSTANCE_H
