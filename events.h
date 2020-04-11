//
// Created by jaakko on 9.4.2020.
//

#ifndef WM_EVENTS_H
#define WM_EVENTS_H

#include <X11/Xlib.h>

void wmKeyPress(XEvent event);
void wmMapRequest(XEvent event);
void wmReparentNotify(XEvent event);
void wmDestroyNotify(XEvent event);
void wmUnmapNotify(XEvent event);

void (*handler[LASTEvent])(XEvent);

#endif //WM_EVENTS_H
