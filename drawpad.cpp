// @COMPILECMD g++ $@ -o test.out -lX11 -lXtst -ggdb
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include <thread>

#define PROGRAM_NAME "drawpad"

int pad_dev_fd;
Display * display;
Window root, window;
int screen_width, screen_height;

bool is_running = false;
int r = 0;	// program exit value

typedef struct {
	char * device_path;
	int min_x;
	int max_x;
	int min_y;
	int max_y;

	int x_span() {
		return max_x - min_x;
	}
	int y_span() {
		return max_y - min_y;
	}
	int x_normal(int x) {
		return x - min_x;
	}
	int y_normal(int y) {
		return y - min_y;
	}
} touchpad_t;

touchpad_t touchpad;


// #######################



[[ noreturn ]]
void quit(int ignore = 0) {
	if (pad_dev_fd) {
		close(pad_dev_fd);
	}
    exit(r);
}

bool init_dev() {
	if (not touchpad.device_path) {
		return false;
	}

	pad_dev_fd = open(touchpad.device_path, O_RDONLY /* | O_NONBLOCK */);
	if (pad_dev_fd == -1) {
		return false;
	}

	ioctl(pad_dev_fd, EVIOCGRAB, (void*)1);


	int abs[6];
	ioctl(pad_dev_fd, EVIOCGABS(ABS_X), abs);
	touchpad.min_x = abs[1];
	touchpad.max_x = abs[2];
	ioctl(pad_dev_fd, EVIOCGABS(ABS_Y), abs);
	touchpad.min_y = abs[1];
	touchpad.max_y = abs[2];

	return true;
}

bool init_x() {
	// --- Display ---
	display = XOpenDisplay(NULL);
    if (!display) {
		return false;
    }

	// --- Transparent window ---
    static XVisualInfo vinfo;
    XMatchVisualInfo(display, DefaultScreen(display), 32, TrueColor, &vinfo);

    static XSetWindowAttributes attr;
    attr.colormap = XCreateColormap(display, DefaultRootWindow(display), vinfo.visual, AllocNone);
    attr.border_pixel = 0;
    attr.background_pixel = 0;

    window = XCreateWindow(display,
		DefaultRootWindow(display),
		0, 0,
		300, 200,
		0, vinfo.depth,
		InputOutput, vinfo.visual,
		CWColormap | CWBorderPixel | CWBackPixel, &attr
	);

	XStoreName(display, window, PROGRAM_NAME " Frame");

    XMapWindow(display, window);

    static Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", 0);
    XSetWMProtocols(display, window, &wm_delete_window, 1);

	// --- Screen ---
    int screen_num = DefaultScreen(display);
    Screen *screen = XScreenOfDisplay(display, screen_num);

    screen_width  = WidthOfScreen(screen);
    screen_height = HeightOfScreen(screen);

	return true;
}

bool init_signal() {
	signal(SIGINT,  quit);
	signal(SIGTERM, quit);
	signal(SIGQUIT, quit);

	return true;
}


// #######################



void handle_dev_event() {
	static struct input_event events[64];
	static int c;
	static int x = 0;
	static int y = 0;

	static bool is_held = false;
	static bool did_update;

	did_update = false;
	c = read(pad_dev_fd, events, sizeof(events)) / sizeof(struct input_event);
	for (int i = 0; i < c; i++) {
		const struct input_event &e = events[i];
		switch (e.code) {
			case ABS_MT_POSITION_X: {
				did_update = true;
				is_held = true;
				x = e.value;
			} break;
			case ABS_MT_POSITION_Y: {
				did_update = true;
				is_held = true;
				y = e.value;
			} break;
			case ABS_PRESSURE: {
				if (e.value == 0) {
					did_update = true;
					is_held = false;
				}
			} break;
		}
	}
	if (did_update) {
		XWindowAttributes attr;
		XGetWindowAttributes(display, window, &attr);

		int x_offset, y_offset;
		Window child_return;

		XTranslateCoordinates(display,
				window, DefaultRootWindow(display),
				0, 0,
				&x_offset, &y_offset,
				&child_return
		);
		x_offset = x_offset + attr.x;
		y_offset = y_offset + attr.y;

		int x_tick = (int)(
			x_offset
				+ (
					(double)attr.width
						/ (
							(double)touchpad.x_span() / (double)touchpad.x_normal(x)
						)
				)
		);
		int y_tick = (int)(
			y_offset
				+ (
					(double)attr.height
						/ (
							(double)touchpad.y_span() / (double)touchpad.y_normal(y)
						)
				)
		);

		printf("Touched: %dx%d -> %dx%d\n", x, y, x_tick, y_tick);

		XTestFakeMotionEvent(display, -1, x_tick, y_tick, CurrentTime);
		XTestFakeButtonEvent(display, Button1, True, CurrentTime);
		if (not is_held) {
			XTestFakeButtonEvent(display, Button1, False, CurrentTime);
		}
		XFlush(display);
	}
}

void handle_window_event() {
	//while (XPending(display)) {
		XEvent event;
		XNextEvent(display, &event);
		switch(event.type) {
			case ClientMessage: {
				if (event.xclient.message_type == XInternAtom(display, "WM_PROTOCOLS", 1)
				&& (Atom)event.xclient.data.l[0] == XInternAtom(display, "WM_DELETE_WINDOW", 1)) {
					quit();
				}
			} break;
		}
	//}
}


// #######################



const char * const help_message =
PROGRAM_NAME " <device-path>\n"
"\tNOTE: candidate devices are usually found under /dev/input/;\n"
"\t       evtest(1) can be used to find the correct one\n"
"\tNOTE: opening an input device and thereby this program require root access\n"
;

signed main(int argc, char * * argv) {
	if (argc != 2) {
		fputs(help_message, stdout);
		return 1;
	}

	touchpad.device_path = argv[1];

	if (not (init_signal() && init_dev() && init_x())) {
		r = 1;
		quit();
	}

	std::thread dev_thread([](){ for(;;){ handle_dev_event(); } });
	for(;;){ handle_window_event(); }

	return 0;
}
