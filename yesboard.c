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
#define SEP 5
#define RESET_KEYCODE 22
#define MAX_NAME 50

struct key {
    unsigned int keycode;
    char *name;
    int nameLen;
    int x;
    int y;
    int fw;
    int fh;
    int pressed;
    int count;
};

struct {
    Display *dpy;
    int screen;
    Window win;
    GC gc;
    XFontStruct *keyfont, *numfont;
    struct key *keys;
} app;

struct key mkKey(int keycode, char *name, int x, int y) {
    int _, asc, desc;
    XCharStruct overall;
    XTextExtents(app.keyfont, name, strlen(name), &_, &asc, &desc, &overall);
    return (struct key){ keycode, name, strlen(name), x, y, overall.width, asc - desc, 0, 0 };
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

#define NUMBUF 10
char numbuf[NUMBUF];

void redraw(struct key key) {
    int x = SIZE*key.x/SCALE, y = SIZE*key.y/SCALE;

    XSetForeground(app.dpy, app.gc, key.pressed ? COLOR_PRESSED : COLOR_RELEASED);
    XFillRectangle(app.dpy, app.win, app.gc, x, y, SIZE, SIZE);

    snprintf(numbuf, NUMBUF, "%d", key.count);
    int _, asc, desc;
    XCharStruct overall;
    XTextExtents(app.numfont, numbuf, strlen(numbuf), &_, &asc, &desc, &overall);
    const int h = SEP + asc - desc;

    XSetForeground(app.dpy, app.gc, COLOR_LABEL);
    XSetFont(app.dpy, app.gc, app.keyfont->fid);
    XDrawString(app.dpy, app.win, app.gc, x + (SIZE - key.fw)/2, y + (SIZE + key.fh - h)/2, key.name, key.nameLen);

    XSetFont(app.dpy, app.gc, app.numfont->fid);
    XDrawString(app.dpy, app.win, app.gc, x + (SIZE - overall.width)/2, y + (SIZE + key.fh + h)/2, numbuf, strlen(numbuf));
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
            if (kev->keycode == RESET_KEYCODE) {
                for (struct key *key = app.keys; key->keycode; ++key) {
                    key->count = 0;
                    redraw(*key);
                }
            } else for (struct key *key = app.keys; key->keycode; ++key) {
                if (key->keycode == kev->keycode) {
                    if (ev.type == key_press_type) {
                        if (!key->pressed) ++key->count;
                        key->pressed = 1;
                    } else {
                        key->pressed = 0;
                    }
                    redraw(*key);
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    app.dpy = NULL;

    if (argc != 3) {
        fprintf(stderr, "usage: %s [id] [conf]\n", argv[0]);
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

    app.keyfont = XLoadQueryFont(app.dpy, "fixed");
    app.numfont = XLoadQueryFont(app.dpy, "-*-fixed-medium-*-*-*-9-*-*-*-*-*-*-*");

    FILE *fp = fopen(argv[2], "r");
    if (!fp) {
        fprintf(stderr, "failed to open conf file\n");
        goto out;
    }

    int nkeys = 10, curkey = 0;
    struct key *keys = malloc(nkeys * sizeof *keys);

    int keycode, x, y, last;
    char namebuf[MAX_NAME+1], *name;
    for (;;) {
        int ret = fscanf(fp, "%d %d %d ", &keycode, &x, &y);

        if (ret == EOF) {
            keys[curkey] = mkKey(0, "", 0, 0);
            break;
        }

        if (ret != 3 || !fgets(namebuf, MAX_NAME, fp) || namebuf[last = strlen(namebuf)-1] != '\n') {
            fprintf(stderr, "invalid conf file\n");
            goto out;
        }

        // intentionally alloc 1 less to strip newline
        name = malloc((last + 1) * sizeof *name);
        strncpy(name, namebuf, last);
        name[strlen(namebuf) - 1] = 0;

        // printf("adding key [%s] code %d at %d %d\n", name, keycode, x, y);
        keys[curkey++] = mkKey(keycode, name, x, y);
        if (curkey >= nkeys) {
            nkeys *= 2;
            keys = realloc(keys, nkeys * sizeof *keys);
        }
    }

    app.keys = keys;
    go();

out:
    if (app.dpy) XCloseDisplay(app.dpy);
    return EXIT_FAILURE;
}
