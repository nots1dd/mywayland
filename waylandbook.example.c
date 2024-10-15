#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "protocols/xdg-shell-client-protocol.h"
#include "protocols/src/xdg-shell-client-protocol.c"

/**********************************************
 * @WAYLAND CLIENT EXAMPLE CODE
 **********************************************
 * 
 * This program implements a simple Wayland client using the Wayland 
 * protocol and the XDG Shell extension. It connects to a Wayland server, 
 * creates a window with a checkerboard pattern, and handles pointer and 
 * keyboard events.
 *
 * @STRUCTURES AND LISTENERS:
 * 
 * 1. **client_state**: A structure that holds the state of the client, 
 * including:
 *    - Wayland display, registry, compositor, and other protocol objects.
 *    - XDG surfaces and top-level windows.
 *    - Pointer and keyboard objects.
 *    - Current pointer event information.
 * 
 * 2. **pointer_event**: A structure that encapsulates information about 
 * pointer events, such as motion, button presses, and axis scrolling.
 *
 * 3. **Listeners**: Functions that respond to various Wayland events.
 *    - **wl_registry_listener**: Listens for global objects added or 
 *      removed (e.g., compositor, SHM).
 *    - **wl_seat_listener**: Listens for capabilities of input devices 
 *      (e.g., pointer, keyboard).
 *    - **wl_pointer_listener**: Listens for pointer events (enter, 
 *      leave, motion, button actions, etc.).
 *    - **wl_keyboard_listener**: Listens for keyboard events (key presses, 
 *      keymap updates, etc.).
 *    - **xdg_surface_listener**: Listens for surface configuration events.
 *    - **xdg_wm_base_listener**: Listens for ping requests from the 
 *      compositor.
 * 
 * @FLOW OF THE PROGRAM:
 * 
 * 1. **Initialization**:
 *    - The program starts by connecting to the Wayland display server and 
 *      obtaining the registry.
 *    - The registry listener is added to receive global objects.
 *    - A round trip to the display server is performed to get the global 
 *      objects.
 *
 * 2. **Creating the Surface**:
 *    - A Wayland surface is created through the compositor.
 *    - An XDG surface is obtained from the XDG shell base, which allows 
 *      for proper window management.
 *    - The surface is configured with a title and is committed to the 
 *      compositor.
 *
 * 3. **Event Loop**:
 *    - The program enters a loop where it dispatches Wayland events. 
 *      This allows the client to respond to pointer and keyboard events as 
 *      they occur.
 *
 * 4. **Pointer Events**:
 *    - The pointer listener captures events related to pointer motion, 
 *      button presses, and axis scrolling.
 *    - Each event updates the `pointer_event` structure in the client state, 
 *      which is then printed to the stderr for debugging.
 *
 * 5. **Keyboard Events**:
 *    - The keyboard listener captures key presses and releases.
 *    - It updates the keyboard state and prints key information to 
 *      the stderr.
 *
 * 6. **Buffer Management**:
 *    - A shared memory buffer is created to store pixel data for drawing.
 *    - The `draw_frame` function is called to render a checkerboard pattern 
 *      onto this buffer.
 *    - The buffer is attached to the surface and committed to be displayed 
 *      on the screen.
 *
 * @CONCLUSION:
 * 
 * This program serves as a basic example of how to create a Wayland client, 
 * handle input events, and render content using shared memory. It demonstrates 
 * the structure of a Wayland client application and how to interact with 
 * the Wayland compositor through various protocols and listeners.
 **********************************************/

enum pointer_event_mask {
       POINTER_EVENT_ENTER = 1 << 0,
       POINTER_EVENT_LEAVE = 1 << 1,
       POINTER_EVENT_MOTION = 1 << 2,
       POINTER_EVENT_BUTTON = 1 << 3,
       POINTER_EVENT_AXIS = 1 << 4,
       POINTER_EVENT_AXIS_SOURCE = 1 << 5,
       POINTER_EVENT_AXIS_STOP = 1 << 6,
       POINTER_EVENT_AXIS_DISCRETE = 1 << 7,
};

struct pointer_event {
       uint32_t event_mask;             // Masks for various event types
       wl_fixed_t surface_x, surface_y; // Pointer coordinates on the surface
       uint32_t button, state;          // Button and its state (pressed/released)
       uint32_t time;                   // Time of the event
       uint32_t serial;                 // Serial number of the event
       struct {
               bool valid;              // Validity of axis value
               wl_fixed_t value;        // Value of the axis (e.g., scroll)
               int32_t discrete;        // Discrete value (for axis)
       } axes[2];                       // Array for two axes (vertical, horizontal)
       uint32_t axis_source;            // Source of the axis event
};

/* Shared memory support code */
static void
randname(char *buf)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A'+(r&15)+(r&16)*2;
        r >>= 5;
    }
}

static int
create_shm_file(void)
{
    int retries = 100;
    do {
        char name[] = "/wl_shm-XXXXXX";
        randname(name + sizeof(name) - 7);
        --retries;
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

static int
allocate_shm_file(size_t size)
{
    int fd = create_shm_file();
    if (fd < 0)
        return -1;
    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Wayland code */
struct client_state {
    /* Globals */
    struct wl_display *wl_display;       // Wayland display connection
    struct wl_registry *wl_registry;     // Global registry for Wayland objects
    struct wl_shm *wl_shm;               // Shared memory object
    struct wl_compositor *wl_compositor; // Compositor interface
    struct xdg_wm_base *xdg_wm_base;     // XDG window manager base interface
    struct wl_seat *wl_seat;             // Input device seat
    /* Objects */
    struct wl_surface *wl_surface;       // Wayland surface
    struct xdg_surface *xdg_surface;     // XDG surface
    struct xdg_toplevel *xdg_toplevel;   // Top-level window
    struct wl_keyboard *wl_keyboard;     // Keyboard object
    struct wl_pointer *wl_pointer;       // Pointer object
    /* State */
    float offset;                        // Offset for drawing (not used here)
    uint32_t last_frame;                 // Last frame number (not used here)
    int width, height;                   // Width and height of the surface
    bool closed;                         // Flag for window closure
    struct pointer_event pointer_event;  // Structure to store current pointer event
    struct xkb_state *xkb_state;         // Keyboard state
    struct xkb_context *xkb_context;     // XKB context for keyboard handling
    struct xkb_keymap *xkb_keymap;       // Keymap for keyboard
};

static void
wl_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    /* Sent by the compositor when it's no longer using this buffer */
    wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};

static struct wl_buffer *
draw_frame(struct client_state *state)
{
    const int width = 640, height = 480;
    int stride = width * 4;
    int size = stride * height;

    int fd = allocate_shm_file(size);
    if (fd == -1) {
        return NULL;
    }

    uint32_t *data = mmap(NULL, size,
            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(state->wl_shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
            width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    /* Draw checkerboxed background */
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if ((x + y / 8 * 8) % 16 < 8)
                data[y * width + x] = 0xFF666666;
            else
                data[y * width + x] = 0xFFEEEEEE;
        }
    }

    munmap(data, size);
    wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
    return buffer;
}

static void
xdg_surface_configure(void *data,
        struct xdg_surface *xdg_surface, uint32_t serial)
{
    struct client_state *state = data;
    xdg_surface_ack_configure(xdg_surface, serial);

    struct wl_buffer *buffer = draw_frame(state);
    wl_surface_attach(state->wl_surface, buffer, 0, 0);
    wl_surface_commit(state->wl_surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};


static void
wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
               uint32_t serial, struct wl_surface *surface,
               wl_fixed_t surface_x, wl_fixed_t surface_y)
{
       struct client_state *client_state = data;
       client_state->pointer_event.event_mask |= POINTER_EVENT_ENTER;
       client_state->pointer_event.serial = serial;
       client_state->pointer_event.surface_x = surface_x,
               client_state->pointer_event.surface_y = surface_y;
}

static void
wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
               uint32_t serial, struct wl_surface *surface)
{
       struct client_state *client_state = data;
       client_state->pointer_event.serial = serial;
       client_state->pointer_event.event_mask |= POINTER_EVENT_LEAVE;
}


static void
wl_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time,
               wl_fixed_t surface_x, wl_fixed_t surface_y)
{
       struct client_state *client_state = data;
       client_state->pointer_event.event_mask |= POINTER_EVENT_MOTION;
       client_state->pointer_event.time = time;
       client_state->pointer_event.surface_x = surface_x,
       client_state->pointer_event.surface_y = surface_y;
}

static void
wl_pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
               uint32_t time, uint32_t button, uint32_t state)
{
       struct client_state *client_state = data;
       client_state->pointer_event.event_mask |= POINTER_EVENT_BUTTON;
       client_state->pointer_event.time = time;
       client_state->pointer_event.serial = serial;
       client_state->pointer_event.button = button,
       client_state->pointer_event.state = state;
}

static void
wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
               uint32_t axis, wl_fixed_t value)
{
       struct client_state *client_state = data;
       client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS;
       client_state->pointer_event.time = time;
       client_state->pointer_event.axes[axis].valid = true;
       client_state->pointer_event.axes[axis].value = value;
}

static void
wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
               uint32_t axis_source)
{
       struct client_state *client_state = data;
       client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS_SOURCE;
       client_state->pointer_event.axis_source = axis_source;
}

static void
wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
               uint32_t time, uint32_t axis)
{
       struct client_state *client_state = data;
       client_state->pointer_event.time = time;
       client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS_STOP;
       client_state->pointer_event.axes[axis].valid = true;
}

static void
wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                          uint32_t axis, int32_t discrete)
{
       struct client_state *client_state = data;
       client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS_DISCRETE;
       client_state->pointer_event.axes[axis].valid = true;
       client_state->pointer_event.axes[axis].discrete = discrete;
}

static void
wl_pointer_frame(void *data, struct wl_pointer *wl_pointer)
{
       struct client_state *client_state = data;
       struct pointer_event *event = &client_state->pointer_event;
       fprintf(stderr, "[DEBUG] pointer frame @ %d: ", event->time);

       if (event->event_mask & POINTER_EVENT_ENTER) {
               fprintf(stderr, "[DEBUG] entered %f, %f ",
                               wl_fixed_to_double(event->surface_x),
                               wl_fixed_to_double(event->surface_y));
       }

       if (event->event_mask & POINTER_EVENT_LEAVE) {
               fprintf(stderr, "leave");
       }

       if (event->event_mask & POINTER_EVENT_MOTION) {
               fprintf(stderr, "[DEBUG] motion %f, %f ",
                               wl_fixed_to_double(event->surface_x),
                               wl_fixed_to_double(event->surface_y));
       }

       if (event->event_mask & POINTER_EVENT_BUTTON) {
               char *state = event->state == WL_POINTER_BUTTON_STATE_RELEASED ?
                       "released" : "pressed";
               fprintf(stderr, "[DEBUG] button %d %s ", event->button, state);
       }

       uint32_t axis_events = POINTER_EVENT_AXIS
               | POINTER_EVENT_AXIS_SOURCE
               | POINTER_EVENT_AXIS_STOP
               | POINTER_EVENT_AXIS_DISCRETE;
       char *axis_name[2] = {
               [WL_POINTER_AXIS_VERTICAL_SCROLL] = "vertical",
               [WL_POINTER_AXIS_HORIZONTAL_SCROLL] = "horizontal",
       };
       char *axis_source[4] = {
               [WL_POINTER_AXIS_SOURCE_WHEEL] = "wheel",
               [WL_POINTER_AXIS_SOURCE_FINGER] = "finger",
               [WL_POINTER_AXIS_SOURCE_CONTINUOUS] = "continuous",
               [WL_POINTER_AXIS_SOURCE_WHEEL_TILT] = "wheel tilt",
       };
       if (event->event_mask & axis_events) {
               for (size_t i = 0; i < 2; ++i) {
                       if (!event->axes[i].valid) {
                               continue;
                       }
                       fprintf(stderr, "[DEBUG] %s axis ", axis_name[i]);
                       if (event->event_mask & POINTER_EVENT_AXIS) {
                               fprintf(stderr, "[DEBUG] value %f ", wl_fixed_to_double(
                                                       event->axes[i].value));
                       }
                       if (event->event_mask & POINTER_EVENT_AXIS_DISCRETE) {
                               fprintf(stderr, "[DEBUG] discrete %d ",
                                               event->axes[i].discrete);
                       }
                       if (event->event_mask & POINTER_EVENT_AXIS_SOURCE) {
                               fprintf(stderr, "[DEBUG] via %s ",
                                               axis_source[event->axis_source]);
                       }
                       if (event->event_mask & POINTER_EVENT_AXIS_STOP) {
                               fprintf(stderr, "(stopped) ");
                       }
               }
       }

       fprintf(stderr, "\n");
       memset(event, 0, sizeof(*event));
}

static const struct wl_pointer_listener wl_pointer_listener = {
       .enter = wl_pointer_enter,
       .leave = wl_pointer_leave,
       .motion = wl_pointer_motion,
       .button = wl_pointer_button,
       .axis = wl_pointer_axis,
       .frame = wl_pointer_frame,
       .axis_source = wl_pointer_axis_source,
       .axis_stop = wl_pointer_axis_stop,
       .axis_discrete = wl_pointer_axis_discrete,
};

static void
wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
               uint32_t serial, struct wl_surface *surface)
{
       fprintf(stderr, "[DEBUG] keyboard leave\n");
}

static void
wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
               uint32_t serial, uint32_t mods_depressed,
               uint32_t mods_latched, uint32_t mods_locked,
               uint32_t group)
{
       struct client_state *client_state = data;
       xkb_state_update_mask(client_state->xkb_state,
               mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void
wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
               int32_t rate, int32_t delay)
{
       /* Left as an exercise for the reader */
}

static void
wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
               uint32_t serial, struct wl_surface *surface,
               struct wl_array *keys)
{
       struct client_state *client_state = data;
       fprintf(stderr, "[DEBUG] keyboard enter; keys pressed are:\n");
       uint32_t *key;
       wl_array_for_each(key, keys) {
               char buf[128];
               xkb_keysym_t sym = xkb_state_key_get_one_sym(
                               client_state->xkb_state, *key + 8);
               xkb_keysym_get_name(sym, buf, sizeof(buf));
               fprintf(stderr, "[DEBUG] sym: %-12s (%d), ", buf, sym);
               xkb_state_key_get_utf8(client_state->xkb_state,
                               *key + 8, buf, sizeof(buf));
               fprintf(stderr, "[DEBUG] utf8: '%s'\n", buf);
       }
}

static void
wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
               uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
       struct client_state *client_state = data;
       char buf[128];
       uint32_t keycode = key + 8;
       xkb_keysym_t sym = xkb_state_key_get_one_sym(
                       client_state->xkb_state, keycode);
       xkb_keysym_get_name(sym, buf, sizeof(buf));
       const char *action =
               state == WL_KEYBOARD_KEY_STATE_PRESSED ? "press" : "release";
       fprintf(stderr, "[DEBUG] key %s: sym: %-12s (%d), ", action, buf, sym);
       xkb_state_key_get_utf8(client_state->xkb_state, keycode,
                       buf, sizeof(buf));
       fprintf(stderr, "[DEBUG] utf8: '%s'\n", buf);
}

static void
wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
               uint32_t format, int32_t fd, uint32_t size)
{
       struct client_state *client_state = data;
       assert(format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);

       char *map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
       assert(map_shm != MAP_FAILED);

       struct xkb_keymap *xkb_keymap = xkb_keymap_new_from_string(
                       client_state->xkb_context, map_shm,
                       XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
       munmap(map_shm, size);
       close(fd);

       struct xkb_state *xkb_state = xkb_state_new(xkb_keymap);
       xkb_keymap_unref(client_state->xkb_keymap);
       xkb_state_unref(client_state->xkb_state);
       client_state->xkb_keymap = xkb_keymap;
       client_state->xkb_state = xkb_state;
}

static const struct wl_keyboard_listener wl_keyboard_listener = {
       .keymap = wl_keyboard_keymap,
       .enter = wl_keyboard_enter,
       .leave = wl_keyboard_leave,
       .key = wl_keyboard_key,
       .modifiers = wl_keyboard_modifiers,
       .repeat_info = wl_keyboard_repeat_info,
};

static void
wl_seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities)
{
        struct client_state *state = data;
        /* TODO */

        bool have_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;

        if (have_pointer && state->wl_pointer == NULL) {
               state->wl_pointer = wl_seat_get_pointer(state->wl_seat);
               wl_pointer_add_listener(state->wl_pointer,
                               &wl_pointer_listener, state);
       } else if (!have_pointer && state->wl_pointer != NULL) {
               wl_pointer_release(state->wl_pointer);
               state->wl_pointer = NULL;
       }
      
      bool have_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;

       if (have_keyboard && state->wl_keyboard == NULL) {
               state->wl_keyboard = wl_seat_get_keyboard(state->wl_seat);
               wl_keyboard_add_listener(state->wl_keyboard,
                               &wl_keyboard_listener, state);
       } else if (!have_keyboard && state->wl_keyboard != NULL) {
               wl_keyboard_release(state->wl_keyboard);
               state->wl_keyboard = NULL;
       }
}

static void
wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
       fprintf(stderr, "seat name: %s\n", name);
}

static const struct wl_seat_listener wl_seat_listener = {
       .capabilities = wl_seat_capabilities,
       .name = wl_seat_name,
};

static void
registry_global(void *data, struct wl_registry *wl_registry,
        uint32_t name, const char *interface, uint32_t version)
{
    struct client_state *state = data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->wl_shm = wl_registry_bind(
                wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->wl_compositor = wl_registry_bind(
                wl_registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base = wl_registry_bind(
                wl_registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->xdg_wm_base,
                &xdg_wm_base_listener, state);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
         state->wl_seat = wl_registry_bind(
                         wl_registry, name, &wl_seat_interface, 7);
         wl_seat_add_listener(state->wl_seat,
                         &wl_seat_listener, state);
    }

}

static void
registry_global_remove(void *data,
        struct wl_registry *wl_registry, uint32_t name)
{
    /* This space deliberately left blank */
}

static const struct wl_registry_listener wl_registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int
main(int argc, char *argv[])
{
    struct client_state state = { 0 };
    state.wl_display = wl_display_connect(NULL);
    state.wl_registry = wl_display_get_registry(state.wl_display);
    state.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    wl_registry_add_listener(state.wl_registry, &wl_registry_listener, &state);
    wl_display_roundtrip(state.wl_display);

    state.wl_surface = wl_compositor_create_surface(state.wl_compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(
            state.xdg_wm_base, state.wl_surface);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_set_title(state.xdg_toplevel, "Example client");
    wl_surface_commit(state.wl_surface);

    while (wl_display_dispatch(state.wl_display)) {
        /* This space deliberately left blank */
    }

    return 0;
}
