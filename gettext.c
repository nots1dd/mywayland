#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>  // For mmap, munmap, PROT_READ, MAP_SHARED, MAP_FAILED
#include <unistd.h>    // For close
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>  // For composing keys

/*******************************************
 * Keymap Handling:
 * - The Wayland protocol provides the keymap (keyboard layout) via a file
 *   descriptor. The client must read and map this keymap into memory.
 * - mmap is used to map the file into memory. This allows efficient reading
 *   of the keymap without loading the entire file into RAM.
 * - xkbcommon, a library for handling keyboard layouts and keymaps, is used
 *   to parse the keymap string and create a new xkb_keymap object.
 * - The old keymap and state, if present, are released to avoid memory leaks.
 * - The new keymap is then used to create a new xkb_state, which tracks
 *   the state of the keyboard (pressed keys, modifiers, etc.).
 *******************************************/

/*******************************************
 * mmap:
 * - Memory maps the keymap file descriptor provided by Wayland.
 * - This allows the client to efficiently read the keymap from the shared memory
 *   without copying the entire file contents into memory.
 * - If mmap fails, it returns MAP_FAILED, indicating an error in mapping.
 *
 * munmap:
 * - After processing, the mapped keymap is unmapped from memory to clean up resources.
 *******************************************/

/*******************************************
 * xkb_keymap_new_from_string:
 * - Parses the mapped keymap string and creates a new xkb_keymap object.
 * - The keymap is in XKB_KEYMAP_FORMAT_TEXT_V1 format, a standard layout format.
 * - This function validates the keymap and returns NULL if the keymap is invalid.
 *
 * xkb_state_new:
 * - Creates a new xkb_state object, representing the state of the keyboard.
 * - The state tracks currently pressed keys, modifiers (Shift, Ctrl, etc.), and
 *   the active keymap. This is essential for translating keycodes into keysyms.
 *******************************************/

/*******************************************
 * keyboard_handle_key:
 * - Handles key press and release events from Wayland.
 * - Wayland keycodes are offset by 8 compared to XKB keycodes, so the code adjusts this.
 * - xkb_state_key_get_syms is used to get the keysym (symbolic representation of the key)
 *   for the given keycode. A key may have multiple symbols based on the state of modifiers.
 * - If Backspace is pressed, it triggers specific behavior (for example, deleting a character).
 * - For other keys, the keysym is converted to a human-readable name, and the state
 *   (pressed/released) is printed for each key.
 *******************************************/

/*******************************************
 * keyboard_handle_enter:
 * - Triggered when the keyboard focus enters a new surface.
 * - The focused surface is tracked for later input handling.
 * - This is essential for ensuring keyboard input is correctly routed to the
 *   current focused surface.
 *******************************************/

/*******************************************
 * keyboard_handle_leave:
 * - Triggered when the keyboard focus leaves the current surface.
 * - The focused surface is cleared, indicating no input is currently focused.
 *******************************************/

/*******************************************
 * keyboard_handle_modifiers:
 * - Handles modifier state updates (Shift, Ctrl, Alt, etc.).
 * - The xkb_state is updated with the new modifier mask, which is essential
 *   for correctly translating keycodes to keysyms based on modifier states.
 *******************************************/

/*******************************************
 * seat_handle_capabilities:
 * - Wayland seats represent input devices (keyboard, pointer, touch).
 * - This callback is triggered when the seat's capabilities are announced.
 * - If the seat has a keyboard capability, the client binds to the keyboard
 *   and sets up the keyboard listener to handle key events.
 * - xkbcommon is initialized to handle keyboard input, including keymaps and state.
 * - If pointer or touch capabilities are present, they are also initialized.
 *******************************************/

/*******************************************
 * seat_handle_name:
 * - This callback provides the name of the seat, which identifies the input
 *   device (e.g., "seat0"). The name is printed for debugging purposes.
 *******************************************/

/*******************************************
 * wl_display_roundtrip:
 * - Sends all pending requests to the server and waits for all responses.
 * - This ensures that the server has processed the initial seat and keyboard
 *   bindings before the client proceeds.
 * - In this case, two roundtrips are performed:
 *   1. To ensure the seat is bound and available.
 *   2. To ensure the keyboard listener receives keymap and modifier events.
 *******************************************/

/*******************************************
 * wl_display_dispatch:
 * - Enters the main event loop, processing incoming Wayland events.
 * - The loop continues until an error occurs (such as the server disconnecting).
 * - This is how the client continuously receives and handles keyboard, pointer,
 *   and other events in real time.
 *******************************************/

/*******************************************
 * Cleanup:
 * - Before exiting, resources are released:
 *   - xkb_state, xkb_keymap, and xkb_context are unreferenced.
 *   - Wayland objects (keyboard, pointer, touch, seat) are destroyed.
 *   - Finally, the client disconnects from the Wayland display to clean up
 *     the connection to the server.
 *******************************************/

struct globals {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;
    struct wl_touch *touch;

    bool error;
    struct xkb_context *xkb_context;
    struct xkb_keymap *keymap;
    struct xkb_state *xkb_state;
    struct wl_surface *focused_surface;  // Track focused surface
};

// Helper function to indicate errors
static void errorOccurred(struct globals *globals) {
    globals->error = true;
}

// Callback for handling the keymap event (opcode 0)
static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {
    struct globals *globals = data;

    // Read the keymap from the provided file descriptor
    char *keymap_string = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (keymap_string == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap keymap\n");
        close(fd);
        return;
    }

    // Create a new keymap using xkbcommon
    struct xkb_keymap *keymap = xkb_keymap_new_from_string(globals->xkb_context, keymap_string, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(keymap_string, size);
    close(fd);

    if (!keymap) {
        fprintf(stderr, "Failed to compile keymap\n");
        errorOccurred(globals);
        return;
    }

    // Replace the old keymap and state if they exist
    if (globals->xkb_state) {
        xkb_state_unref(globals->xkb_state);
    }
    if (globals->keymap) {
        xkb_keymap_unref(globals->keymap);
    }

    globals->keymap = keymap;
    globals->xkb_state = xkb_state_new(keymap);
    if (!globals->xkb_state) {
        fprintf(stderr, "Failed to create XKB state\n");
        errorOccurred(globals);
    }
}

// Callback for handling keyboard key events
static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                       uint32_t time, uint32_t key, uint32_t state) {
    struct globals *globals = data;

    // Convert Wayland keycode to XKB keycode
    uint32_t keycode = key + 8;
    const xkb_keysym_t *syms;
    int num_syms = xkb_state_key_get_syms(globals->xkb_state, keycode, &syms);
    if (num_syms > 0) {
        // Check if the key pressed is Backspace
        if (syms[0] == XKB_KEY_BackSpace) {
            if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
                printf("Backspace pressed\n");
                // You can handle backspace logic here (e.g., remove character from input buffer)
            } else {
                printf("Backspace released\n");
            }
        } else {
            // Handle other keys
            char name[64];
            xkb_keysym_get_name(syms[0], name, sizeof(name));
            printf("Key %s %s\n", name, state == WL_KEYBOARD_KEY_STATE_PRESSED ? "pressed" : "released");
        }
    }
}


// Callback for handling keyboard focus entering
static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                  struct wl_surface *surface, struct wl_array *keys) {
    struct globals *globals = data;

    globals->focused_surface = surface;  // Track the focused surface
    printf("Keyboard entered a surface\n");
}

// Callback for handling keyboard focus leaving
static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                  struct wl_surface *surface) {
    struct globals *globals = data;

    globals->focused_surface = NULL;  // Clear the focused surface
    printf("Keyboard left a surface\n");
}

// Callback for handling keyboard modifiers (Shift, Ctrl, etc.)
static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                      uint32_t mods_depressed, uint32_t mods_latched, 
                                      uint32_t mods_locked, uint32_t group) {
    struct globals *globals = data;
    xkb_state_update_mask(globals->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

// The keyboard listener struct
static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,  // Keymap event (opcode 0)
    .enter = keyboard_handle_enter,    // Enter event
    .leave = keyboard_handle_leave,    // Leave event
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
};

// Callback for seat capabilities (pointer, keyboard, touch)
static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct globals *globals = data;

    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        globals->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(globals->keyboard, &keyboard_listener, globals);
        printf("Keyboard capability present\n");

        // Initialize xkbcommon for keyboard handling
        globals->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!globals->xkb_context) {
            fprintf(stderr, "Failed to create XKB context\n");
            errorOccurred(globals);
            return;
        }
    } else {
        globals->keyboard = NULL;
        errorOccurred(globals);
    }

    if (caps & WL_SEAT_CAPABILITY_POINTER) {
        globals->pointer = wl_seat_get_pointer(seat);
        printf("Pointer capability present\n");
    } else {
        globals->pointer = NULL;
    }

    if (caps & WL_SEAT_CAPABILITY_TOUCH) {
        globals->touch = wl_seat_get_touch(seat);
        printf("Touch capability present\n");
    } else {
        globals->touch = NULL;
    }
}

// Callback for seat name
static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
    printf("Seat name: %s\n", name);
}

// The seat listener struct
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

// Callback for registry global event
static void registry_handler(void *data, struct wl_registry *registry,
                             uint32_t id, const char *interface, uint32_t version) {
    struct globals *globals = data;

    if (strcmp(interface, "wl_seat") == 0) {
        globals->seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
        wl_seat_add_listener(globals->seat, &seat_listener, globals);
        printf("Seat bound\n");
    }
}

// Callback for registry global remove event
static void registry_remover(void *data, struct wl_registry *registry, uint32_t id) {
    // Handle removal if necessary
}

// The registry listener struct
static const struct wl_registry_listener registry_listener = {
    registry_handler,
    registry_remover,
};

int main() {
    // Initialize globals struct
    struct globals globals = {0};

    // Connect to Wayland display
    globals.display = wl_display_connect(NULL);
    if (!globals.display) {
        fprintf(stderr, "Unable to connect to Wayland display\n");
        return -1;
    }

    // Get the registry and add a listener
    globals.registry = wl_display_get_registry(globals.display);
    wl_registry_add_listener(globals.registry, &registry_listener, &globals);

    // Perform a roundtrip to process events
    wl_display_roundtrip(globals.display);

    // Ensure that the seat has been bound
    if (!globals.seat) {
        fprintf(stderr, "Seat is NULL\n");
        wl_display_disconnect(globals.display);
        return -1;
    }

    // Perform a second roundtrip to ensure seat listener gets events
    wl_display_roundtrip(globals.display);

    // Main loop: process Wayland events
    while (wl_display_dispatch(globals.display) != -1 && !globals.error) {
        // Process Wayland events in a loop
    }

    // Cleanup
    if (globals.xkb_state) {
        xkb_state_unref(globals.xkb_state);
    }
    if (globals.keymap) {
        xkb_keymap_unref(globals.keymap);
    }
    if (globals.xkb_context) {
        xkb_context_unref(globals.xkb_context);
    }
    if (globals.keyboard) {
        wl_keyboard_destroy(globals.keyboard);
    }
    if (globals.pointer) {
        wl_pointer_destroy(globals.pointer);
    }
    if (globals.touch) {
        wl_touch_destroy(globals.touch);
    }
    wl_seat_destroy(globals.seat);
    wl_display_disconnect(globals.display);

    return 0;
}
