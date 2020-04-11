//
// Created by jaakko on 10.4.2020.
//

#include "instance.h"
#include "logger.h"
#include "input.h"
#include "events.h"
#include "util.h"

#include <stdlib.h>

#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

wmWindow* wmActiveWindow = NULL;
wmWindow* wmHead = NULL;
wmWindow* wmTail = NULL;

int wmRunning = True;

static int wmDetected = 0;
static int onWMDetected(Display* d, XErrorEvent* e) {
    wmDetected = 1;
    return 0;
}
static int errorHandler(Display* d, XErrorEvent* e) {
    logmsg("Error: %x", e->error_code);
    return 0;
}
int wmInitialize() {
    wmDisplay = XOpenDisplay(NULL);
    if (!wmDisplay) {
        return 0;
    }

    int screen = DefaultScreen(wmDisplay);
    wmRoot = RootWindow(wmDisplay, screen);
    wmScreenWidth = DisplayWidth(wmDisplay, screen);
    wmScreenHeight = DisplayHeight(wmDisplay, screen);

    XSetErrorHandler(onWMDetected);
    XSelectInput(wmDisplay, wmRoot, SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(wmDisplay, False);
    if (wmDetected) {
        XCloseDisplay(wmDisplay);
        return 0;
    }

    XSetErrorHandler(errorHandler);

    // Visuals
    XVisualInfo *infos;
    XRenderPictFormat *fmt;
    int nitems;
    int i;

    XVisualInfo tpl = {
            .screen = screen,
            .depth = 32,
            .class = TrueColor
    };
    long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;
    infos = XGetVisualInfo(wmDisplay, masks, &tpl, &nitems);
    wmVisual = NULL;
    for (i = 0; i < nitems; i++) {
        fmt = XRenderFindVisualFormat(wmDisplay, infos[i].visual);
        if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
            wmVisual = infos[i].visual;
            wmDepth = infos[i].depth;
            wmColormap = XCreateColormap(wmDisplay, wmRoot, wmVisual, AllocNone);
            break;
        }
    }
    XFree(infos);
    if (!wmVisual) {
        wmVisual = DefaultVisual(wmDisplay, screen);
        wmDepth = DefaultDepth(wmDisplay, screen);
        wmColormap = DefaultColormap(wmDisplay, screen);
    }

    wmCursor = XCreateFontCursor(wmDisplay, XC_arrow);
    XDefineCursor(wmDisplay, wmRoot, wmCursor);

    initializeKeyBindings();

    return 1;
}
void wmRun() {
    XEvent event;
    while (wmRunning) {
        XNextEvent(wmDisplay, &event);
        if (handler[event.type])
            handler[event.type](event);
    }
}
void wmFree() {
    freeKeyBindings();

    wmWindow* next;
    for (wmWindow* wmWindow = wmHead; wmWindow; wmWindow = next) {
        next = wmWindow->next;
        free(wmWindow);
    }

    XFreeCursor(wmDisplay, wmCursor);

    XCloseDisplay(wmDisplay);
}

void wmFocusWindow(wmWindow* window) {
    XMapWindow(wmDisplay, window->frame);
    XRaiseWindow(wmDisplay, window->frame);
    XSetInputFocus(wmDisplay, window->window, RevertToPointerRoot, CurrentTime);
    XUnmapWindow(wmDisplay, wmActiveWindow->frame);
    wmActiveWindow = window;
}
void wmRequestCloseWindow(wmWindow* window) {
    Atom* supportedProtocols;
    int numSupportedProtocols;
    Atom WM_DELETE_WINDOW = XInternAtom(wmDisplay, "WM_DELETE_WINDOW", False);
    if (XGetWMProtocols(wmDisplay, window->window, &supportedProtocols, &numSupportedProtocols)) {
        for (int i = 0; i < numSupportedProtocols; i++) {
            if (supportedProtocols[i] == WM_DELETE_WINDOW) {
                XEvent message;
                message.xclient.type = ClientMessage;
                message.xclient.message_type = XInternAtom(wmDisplay, "WM_PROTOCOLS", False);
                message.xclient.window = window->window;
                message.xclient.format = 32;
                message.xclient.data.l[0] = WM_DELETE_WINDOW;
                XSendEvent(wmDisplay, window->window, False, 0, &message);
            }
        }
    }
    else {
        XKillClient(wmDisplay, window->window);
    }
}

static void attachWindow(wmWindow* window) {
    window->next = wmHead;

    if (!wmHead) {
        wmTail = window;
    }
    if (wmHead) {
        wmHead->previous = window;
    }
    wmHead = window;
}
static void detachWindow(wmWindow* window) {
    if (!window->previous) {
        wmHead = window->next;
        if (wmHead) {
            wmHead->previous = NULL;
        }
    }
    else {
        window->previous->next = window->next;
    }

    if (!window->next) {
        wmTail = window->previous;
        if (wmTail) {
            wmTail->next = NULL;
        }
    }
    else {
        window->next->previous = window->previous;
    }
}
void wmNewWindow(Window window) {
    XWindowAttributes attr;
    XGetWindowAttributes(wmDisplay, window, &attr);
    if (attr.override_redirect) {
        return;
    }

    int width = MIN(MAX(attr.width, 1920), wmScreenWidth - 200);
    int height = MIN(MAX(attr.height, 1080), wmScreenHeight - 200);
    int x = wmScreenWidth / 2 - width / 2;
    int y = wmScreenHeight / 2 - height / 2;

    if (attr.width != width || attr.height != height) {
        XResizeWindow(wmDisplay, window, width, height);
    }

    XSetWindowAttributes frameAttr;
    frameAttr.colormap = wmColormap;
    frameAttr.border_pixel = 0x80789F43;
    frameAttr.background_pixel = 0;

    Window frame = XCreateWindow(
            wmDisplay,
            wmRoot,
            x,
            y,
            width,
            height,
            4,
            wmDepth,
            CopyFromParent,
            wmVisual,
            CWColormap | CWBorderPixel | CWBackPixel,
            &frameAttr
    );

    XSelectInput(wmDisplay, frame, SubstructureNotifyMask | SubstructureRedirectMask);

    XReparentWindow(wmDisplay, window, frame, 0, 0);

    wmWindow* old_wmWindow = wmActiveWindow;
    wmWindow* new_wmWindow = calloc(1, sizeof(wmWindow));
    new_wmWindow->window = window;
    new_wmWindow->frame = frame;
    attachWindow(new_wmWindow);
    wmActiveWindow = new_wmWindow;

    XMapWindow(wmDisplay, window);
    XMapWindow(wmDisplay, frame);
    XMoveWindow(wmDisplay, frame, x, y);
    XRaiseWindow(wmDisplay, frame);
    XSetInputFocus(wmDisplay, window, RevertToPointerRoot, CurrentTime);

    if (old_wmWindow) {
        XUnmapWindow(wmDisplay, old_wmWindow->frame);
    }
}
void wmFreeWindow(wmWindow* window) {
    XUnmapWindow(wmDisplay, window->frame);
    XUnmapWindow(wmDisplay, window->window);
    XReparentWindow(wmDisplay, window->window, wmRoot, 0, 0);
    XDestroyWindow(wmDisplay, window->frame);

    detachWindow(window);
    if (window == wmActiveWindow) {
        wmActiveWindow = wmActiveWindow->next ? wmActiveWindow->next : wmActiveWindow->previous;
    }
    free(window);

    if (wmActiveWindow) {
        XMapWindow(wmDisplay, wmActiveWindow->frame);
        XRaiseWindow(wmDisplay, wmActiveWindow->frame);
        XSetInputFocus(wmDisplay, wmActiveWindow->window, RevertToPointerRoot, CurrentTime);
    }
}
wmWindow* wmWindowTowmWindow(Window window) {
    wmWindow* w;
    for (w = wmHead; w && window != w->window; w = w->next);
    return w;
}
