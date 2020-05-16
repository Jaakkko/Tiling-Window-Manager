//
// Created by jaakko on 29.4.2020.
//

#ifndef WM_BAR_H
#define WM_BAR_H

extern Window wmBarWindow;

extern unsigned wmBarHeight;

void wmCreateBar();
void wmUpdateBar();
void wmExposeBar();
void wmUpdateBarBounds();
void wmDestroyBar();

#endif //WM_BAR_H
