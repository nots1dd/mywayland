#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-client.h> // Wayland client API for interacting with the Wayland server
#include <wayland-cursor.h> // Wayland cursor support for cursor management
#include "protocols/xdg-shell-client-protocol.h" // XDG shell protocol for window management
#include "protocols/src/xdg-shell-client-protocol.c" // Implementation of the stable version of XDG shell protocol

/************************************************
 * Global Variables Declaration
 * These variables hold references to various Wayland objects
 ************************************************/
struct wl_compositor *compositor = NULL; // Compositor for creating surfaces
struct wl_seat *seat = NULL; // Represents input devices (like keyboards and mice)
struct wl_shm *shm = NULL; // Shared memory for buffer allocation
struct xdg_wm_base *wm_base = NULL; // XDG shell base for window management
struct wl_surface *cursor_surface; // Surface for the cursor
struct wl_cursor_image *cursor_image; // Image representation of the cursor
struct wl_pointer *pointer; // Pointer object to handle mouse events

/************************************************
 * Registry Global Handler
 * This function is called whenever a new global object (like compositor, shm, etc.) is available
 ************************************************/
void registry_global_handler(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    printf("[LOG] Received interface: %s (version: %d)\n", interface, version);

    // Bind to the compositor interface
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 3);
        printf("[SUCCESS] Bound to wl_compositor\n");
    } 
    // Bind to the shared memory interface
    else if (strcmp(interface, "wl_shm") == 0) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        printf("[SUCCESS] Bound to wl_shm\n");
    } 
    // Bind to the input seat interface
    else if (strcmp(interface, "wl_seat") == 0) {
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        printf("[SUCCESS] Bound to wl_seat\n");
    } 
    // Bind to the XDG window manager interface
    else if (strcmp(interface, "xdg_wm_base") == 0) {
        wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        printf("[SUCCESS] Bound to xdg_wm_base\n");
    }
}

/************************************************
 * Registry Global Remove Handler
 * This function is called when a global object is removed
 ************************************************/
void registry_global_remove_handler(void *data, struct wl_registry *registry, uint32_t name) {}

const struct wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler
};

/************************************************
 * XDG Toplevel Configure Handler
 * Called when the toplevel window is configured (e.g., resized)
 ************************************************/
void xdg_toplevel_configure_handler(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {
    printf("Configure: %dx%d\n", width, height);
}

/************************************************
 * XDG Toplevel Close Handler
 * Called when the toplevel window is closed
 ************************************************/
void xdg_toplevel_close_handler(void *data, struct xdg_toplevel *xdg_toplevel) {
    printf("Toplevel closed\n");
}

/************************************************
 * XDG Toplevel Listener
 * Listeners to handle events related to the toplevel window
 ************************************************/
const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure_handler,
    .close = xdg_toplevel_close_handler
};

/************************************************
 * Pointer Event Handlers
 * These functions handle mouse pointer events
 ************************************************/
void pointer_enter_handler(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
    wl_pointer_set_cursor(pointer, serial, cursor_surface, cursor_image->hotspot_x, cursor_image->hotspot_y);
    printf("[DEBUG] Pointer entered: %d %d\n", wl_fixed_to_int(x), wl_fixed_to_int(y));
}

void pointer_leave_handler(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface) {}

void pointer_motion_handler(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    printf("[DEBUG] Pointer motion: %d %d\n", wl_fixed_to_int(x), wl_fixed_to_int(y));
}

void pointer_button_handler(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    printf("[DEBUG] Button pressed: 0x%x state: %d\n", button, state);
}

void pointer_axis_handler(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    printf("[DEBUG] Axis movement: %d %f\n", axis, wl_fixed_to_double(value));
}

/************************************************
 * Pointer Listener
 * Contains all pointer event handlers
 ************************************************/
const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter_handler,
    .leave = pointer_leave_handler,
    .motion = pointer_motion_handler,
    .button = pointer_button_handler,
    .axis = pointer_axis_handler
};

/************************************************
 * Main Function
 * This is where the Wayland client starts executing
 ************************************************/
int main(void) {
    // Connect to the Wayland display server
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to the display\n");
        return EXIT_FAILURE;
    }

    // Get the registry for global objects
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    // Perform a roundtrip to retrieve global objects
    wl_display_roundtrip(display);
    
    // Get the pointer associated with the seat and add event listener
    pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(pointer, &pointer_listener, NULL);

    // Check for required interfaces
    if (!compositor) {
        fprintf(stderr, "wl_compositor not available\n");
        return EXIT_FAILURE;
    }
    if (!wm_base) {
        fprintf(stderr, "xdg_wm_base not available\n");
        return EXIT_FAILURE;
    }
    if (!seat) {
        fprintf(stderr, "wl_seat not available\n");
        return EXIT_FAILURE;
    }
    if (!shm) {
        fprintf(stderr, "wl_shm not available\n");
        return EXIT_FAILURE;
    }

    printf("All required interfaces are available.\n");

    // Create a Wayland surface and associate it with the XDG shell
    struct wl_surface *surface = wl_compositor_create_surface(compositor);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    struct xdg_toplevel *xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

    // Add listeners for the xdg_toplevel events
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

    // Set the title of the window
    xdg_toplevel_set_title(xdg_toplevel, "My Wayland Client");

    /************************************************
     * Configure Shared Memory for Buffer Allocation
     * The following code sets up a shared memory buffer for drawing
     ************************************************/
    int width = 200; // Width of the window
    int height = 200; // Height of the window
    int stride = width * 4; // Stride in bytes (4 bytes per pixel)
    int size = stride * height; // Total size in bytes

    // Create an anonymous file for shared memory
    int fd = syscall(SYS_memfd_create, "buffer", 0);
    ftruncate(fd, size); // Set the size of the file

    // Map the file into memory
    unsigned char *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    memset(data, 0, size); // Clear the memory buffer

    // Create a shared memory pool from the file descriptor
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);

    // Allocate a buffer in the shared memory pool
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);

    // Fill the buffer with a yellow color
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            struct pixel {
                unsigned char blue; // Blue component
                unsigned char green; // Green component
                unsigned char red; // Red component
                unsigned char alpha; // Alpha component
            } *px = (struct pixel *)(data + y * stride + x * 4);

            // Set pixel color to yellow (ARGB)
            px->alpha = 255; // Fully opaque
            px->red = 255; // Max red
            px->green = 255; // Max green
            px->blue = 0; // No blue
        }
    }

    // Load cursor theme and get the cross cursor image
    struct wl_cursor_theme *cursor_theme = wl_cursor_theme_load("Breeze_Light", 24, shm);
    struct wl_cursor *cursor = wl_cursor_theme_get_cursor(cursor_theme, "cross");
    cursor_image = cursor->images[0]; // Use the first image for the cursor
    struct wl_buffer *cursor_buffer = wl_cursor_image_get_buffer(cursor_image); // Get cursor buffer

    /************************************************
     * Initial Commit to the Surface
     * Attach the buffer to the surface and commit the changes
     ************************************************/
    
    wl_surface_commit(surface);

    wl_surface_attach(surface, buffer, 0, 0); // Attach the buffer to the surface
    wl_surface_commit(surface); // Commit the surface changes to the Wayland compositor

    // Main event loop
    while (1) {
        wl_display_dispatch(display); // Dispatch events from the display
    }

    /************************************************
     * Cleanup Resources
     * (This code will never be reached in the current loop)
     ************************************************/
    munmap(data, size); // Unmap the shared memory
    wl_buffer_destroy(buffer); // Destroy the buffer
    wl_shm_pool_destroy(pool); // Destroy the shared memory pool
    wl_display_disconnect(display); // Disconnect from the Wayland display server

    return EXIT_SUCCESS; // Exit the program
}
