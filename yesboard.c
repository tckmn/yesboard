/*
 * yesboard - a nohboard-inspired key input display for X
 * Copyright (C) 2023  Andy Tockman <andy@tck.mn>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * based on code from the xinput utility, which is licensed as follows:
 *
 * Copyright 1996 by Frederic Lepied, France. <Frederic.Lepied@sugix.frmug.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is  hereby granted without fee, provided that
 * the  above copyright   notice appear  in   all  copies and  that both  that
 * copyright  notice   and   this  permission   notice  appear  in  supporting
 * documentation, and that   the  name of  the authors  not  be  used  in
 * advertising or publicity pertaining to distribution of the software without
 * specific,  written      prior  permission.     The authors  make  no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * THE AUTHORS DISCLAIM ALL   WARRANTIES WITH REGARD  TO  THIS SOFTWARE,
 * INCLUDING ALL IMPLIED   WARRANTIES OF MERCHANTABILITY  AND   FITNESS, IN NO
 * EVENT  SHALL THE AUTHORS  BE   LIABLE   FOR ANY  SPECIAL, INDIRECT   OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA  OR PROFITS, WHETHER  IN  AN ACTION OF  CONTRACT,  NEGLIGENCE OR OTHER
 * TORTIOUS  ACTION, ARISING    OUT OF OR   IN  CONNECTION  WITH THE USE    OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLOR_PRESSED  0x585858
#define COLOR_RELEASED 0x181818
#define COLOR_LABEL    0xffffff
#define SIZE 30
#define SCALE 2

struct key {
    unsigned int keycode;
    char *name;
    int nameLen;
    int x;
    int y;
    int fx;
    int fy;
    int pressed;
};

struct {
    Display *dpy;
    int screen;
    Window win;
    GC gc;
    XFontStruct *font;
    struct key *keys;
} app;

struct key mkKey(int keycode, char *name, int x, int y) {
    int _, asc, desc;
    XCharStruct overall;
    XTextExtents(app.font, name, strlen(name), &_, &asc, &desc, &overall);
    return (struct key){ keycode, name, strlen(name), x, y, -overall.width, asc - desc,  0 };
}

int key_press_type = -1;
int key_release_type = -1;

int register_events(int id) {
    unsigned long screen = DefaultScreen(app.dpy);
    Window root_win = RootWindow(app.dpy, screen);
    XDevice *device = XOpenDevice(app.dpy, id);

    if (!device) {
        fprintf(stderr, "unable to open device %d\n", id);
        return 0;
    }

    if (!device->num_classes) {
        fprintf(stderr, "device %d has no classes\n", id);
        return 0;
    }

    XEventClass event_list[2];
    for (int i = 0; i < device->num_classes; ++i) {
        if (device->classes[i].input_class == KeyClass) {
            DeviceKeyPress(device, key_press_type, event_list[0]);
            DeviceKeyRelease(device, key_release_type, event_list[1]);
            goto found;
        }
    }

    fprintf(stderr, "no key class found on device %d\n", id);
    return 0;

found:
    if (XSelectExtensionEvent(app.dpy, root_win, event_list, 2)) {
        fprintf(stderr, "error selecting extended events\n");
        return 0;
    }

    return 1;
}

void redraw(struct key key) {
    int x = SIZE*key.x/SCALE, y = SIZE*key.y/SCALE;

    XSetForeground(app.dpy, app.gc, key.pressed ? COLOR_PRESSED : COLOR_RELEASED);
    XFillRectangle(app.dpy, app.win, app.gc, x, y, SIZE, SIZE);

    XSetForeground(app.dpy, app.gc, COLOR_LABEL);
    XDrawString(app.dpy, app.win, app.gc, x + (SIZE + key.fx)/2, y + (SIZE + key.fy)/2, key.name, key.nameLen);
}

void go() {
    app.screen = DefaultScreen(app.dpy);
    unsigned long black = BlackPixel(app.dpy, app.screen);

    int maxx = 0, maxy = 0;
    for (struct key *key = app.keys; key->keycode; ++key) {
        if (key->x > maxx) maxx = key->x;
        if (key->y > maxy) maxy = key->y;
    }

    app.win = XCreateSimpleWindow(app.dpy, DefaultRootWindow(app.dpy), 0, 0,
            SIZE*maxx/SCALE + SIZE, SIZE*maxy/SCALE + SIZE, 0, black, black);

    XSelectInput(app.dpy, app.win, ExposureMask);
    app.gc = XCreateGC(app.dpy, app.win, 0, 0);

    // magic to make window floating
    Atom a1 = XInternAtom(app.dpy, "_NET_WM_WINDOW_TYPE", 0),
         a2 = XInternAtom(app.dpy, "_NET_WM_WINDOW_TYPE_DIALOG", 0);
    XChangeProperty(app.dpy, app.win, a1, a1, 4 * sizeof a2, PropModeReplace, (unsigned char*) &a2, 1);

    XMapWindow(app.dpy, app.win);

    XEvent ev;
    while(1) {
        XNextEvent(app.dpy, &ev);

        if (ev.type == Expose) {
            for (struct key *key = app.keys; key->keycode; ++key) {
                redraw(*key);
            }
        } else if ((ev.type == key_press_type) || (ev.type == key_release_type)) {
            XDeviceKeyEvent *kev = (XDeviceKeyEvent *) &ev;
            for (struct key *key = app.keys; key->keycode; ++key) {
                if (key->keycode == kev->keycode) {
                    key->pressed = ev.type == key_press_type ? 1 : 0;
                    redraw(*key);
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    app.dpy = NULL;

    if (argc != 2) {
        fprintf(stderr, "usage: %s [id]\n", argv[0]);
        goto out;
    }

    app.dpy = XOpenDisplay(NULL);

    if (app.dpy == NULL) {
        fprintf(stderr, "Unable to connect to X server\n");
        goto out;
    }

    /* if (!XQueryExtension(app.dpy, "XInputExtension", 0, 0, 0)) { */
    /*     fprintf(stderr, "X Input extension not available.\n"); */
    /*     goto out; */
    /* } */

    Bool hasDetectableAutoRepeat;
    XkbSetDetectableAutoRepeat(app.dpy, True, &hasDetectableAutoRepeat);
    if (!hasDetectableAutoRepeat) {
        fprintf(stderr, "missing detectable auto repeat\n");
        goto out;
    }

    if (!register_events(atoi(argv[1]))) {
        fprintf(stderr, "failed to register events\n");
        goto out;
    }

    app.font = XLoadQueryFont(app.dpy, "fixed");

    struct key keys[12] = {
        mkKey(25, "W", 2, 0),
        mkKey(26, "E", 4, 0),

        mkKey(38, "A", 0, 2),
        mkKey(39, "S", 2, 2),
        mkKey(40, "D", 4, 2),
        mkKey(41, "F", 6, 2),

        mkKey(31, "I", 11, 0),
        mkKey(44, "J", 9, 2),
        mkKey(45, "K", 11, 2),
        mkKey(46, "L", 13, 2),

        mkKey(65, "spc", 6, 4),

        mkKey(0, "", 0, 0)
    };
    app.keys = keys;

    go();

out:
    if (app.dpy) XCloseDisplay(app.dpy);
    return EXIT_FAILURE;
}
