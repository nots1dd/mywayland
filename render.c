#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "protocols/xdg-shell-client-protocol.h"
#include "protocols/src/xdg-shell-client-protocol.c"

/*******************************************
 * Global structures and variables:
 * - `globals` structure holds all Wayland-related objects like display, compositor, surface, etc.
 * - `egl_display`, `egl_context`, `egl_surface` are EGL variables for managing OpenGL rendering.
 *******************************************/
struct globals {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_surface *surface;
    struct wl_egl_window *egl_window;
    struct xdg_wm_base *wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
};

// EGL global variables
EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;

// Simple triangle vertices for rendering
static const GLfloat vertices[] = {
    0.0f,  0.5f,  // Top vertex
   -0.5f, -0.5f,  // Bottom left vertex
    0.5f, -0.5f   // Bottom right vertex
};

/*******************************************
 * Wayland registry handler function:
 * - `registry_handler` listens to Wayland server events for new global objects.
 * - Based on the interface type (like compositor or xdg_wm_base), it binds them to the `globals` struct.
 *******************************************/
static void registry_handler(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    struct globals *globals = data;

    // If the interface is "wl_compositor", bind the compositor object
    if (strcmp(interface, "wl_compositor") == 0) {
        globals->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
        printf("Compositor bound\n");
    } 
    // If the interface is "xdg_wm_base", bind the xdg_wm_base (window manager base) object
    else if (strcmp(interface, "xdg_wm_base") == 0) {
        globals->wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        printf("xdg_wm_base bound\n");
    }
}

/*******************************************
 * Wayland registry removal handler:
 * - `registry_remover` is a placeholder for when global objects are removed.
 *******************************************/
static void registry_remover(void *data, struct wl_registry *registry, uint32_t id) {
    // Handle object removal if necessary (not used in this example)
}

/*******************************************
 * Wayland registry listener:
 * - Contains the `registry_handler` and `registry_remover` functions to handle Wayland registry events.
 *******************************************/
static const struct wl_registry_listener registry_listener = {
    registry_handler,
    registry_remover
};

/*******************************************
 * Initialize EGL:
 * - Sets up the EGL display, context, and surface.
 * - EGL is used to manage OpenGL ES rendering surfaces in Wayland.
 *******************************************/
void init_egl(struct globals *globals) {
    // Get the EGL display connection using Wayland's display
    egl_display = eglGetDisplay((EGLNativeDisplayType)globals->display);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        exit(EXIT_FAILURE);
    }

    // Initialize the EGL display
    if (!eglInitialize(egl_display, NULL, NULL)) {
        fprintf(stderr, "Failed to initialize EGL\n");
        exit(EXIT_FAILURE);
    }

    // EGL configuration: specifies rendering type and color depth
    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,  // OpenGL ES 2.0 support
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,         // Windowed rendering
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config;
    EGLint num_configs;
    eglChooseConfig(egl_display, attribs, &config, 1, &num_configs);  // Choose the appropriate config

    // Create an EGL context for OpenGL ES 2.0
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,  // OpenGL ES 2.0 context
        EGL_NONE
    };
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        exit(EXIT_FAILURE);
    }

    // Create an EGL window surface (bind it to Wayland's surface)
    globals->egl_window = wl_egl_window_create(globals->surface, 900, 900);
    egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)globals->egl_window, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Failed to create EGL surface\n");
        exit(EXIT_FAILURE);
    }

    // Make the EGL context current
    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "Failed to make EGL context current\n");
        exit(EXIT_FAILURE);
    }
    
    // Set the OpenGL viewport to match the window size (900x900)
    glViewport(0, 0, 900, 900);
}

/*******************************************
 * Check for OpenGL errors:
 * - This function loops through and prints any OpenGL errors found.
 *******************************************/
void check_gl_error(const char *label) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        fprintf(stderr, "OpenGL error (%s): %d\n", label, err);
    }
}

/*******************************************
 * Event handler for xdg_surface configuration:
 * - This handles resizing or other configuration changes.
 * - Acknowledges the configuration and prepares for rendering.
 *******************************************/
static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    struct globals *globals = data;

    // Acknowledge the configuration from the Wayland compositor
    xdg_surface_ack_configure(surface, serial);

    // Make the EGL surface current to render the new frame
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
}

/*******************************************
 * xdg_surface listener:
 * - This structure points to the `xdg_surface_configure` function, which handles configuration events.
 *******************************************/
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

/*******************************************
 * Render a simple triangle using OpenGL ES:
 * - This function sets up shaders and renders a colored triangle.
 *******************************************/
void render_triangle() {
    // Clear the color buffer with black background
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Create and compile the vertex shader
    const char *vertex_shader_source =
        "attribute vec2 position;\n"
        "void main() {\n"
        "    gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n";
    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader(vertex_shader);

    // Check for vertex shader compile errors
    GLint compile_status;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compile_status);
    if (compile_status == GL_FALSE) {
        fprintf(stderr, "Vertex shader compilation failed\n");
        exit(EXIT_FAILURE);
    }

    // Create and compile the fragment shader (outputs red color)
    const char *fragment_shader_source =
        "void main() {\n"
        "    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"  // Red color output
        "}\n";
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
    glCompileShader(fragment_shader);

    // Check for fragment shader compile errors
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compile_status);
    if (compile_status == GL_FALSE) {
        fprintf(stderr, "Fragment shader compilation failed\n");
        exit(EXIT_FAILURE);
    }

    // Link the vertex and fragment shaders into a program
    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);
    glUseProgram(shader_program);  // Use the shader program

    // Bind the triangle vertex positions to the shader's "position" attribute
    GLint position_location = glGetAttribLocation(shader_program, "position");
    glVertexAttribPointer(position_location, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(position_location);

    // Draw the triangle (3 vertices)
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Swap buffers (render the triangle on the screen)
    eglSwapBuffers(egl_display, egl_surface);
}

/*******************************************
 * Main function:
 * - Connects to the Wayland display server, initializes EGL, and enters the rendering loop.
 *******************************************/
int main(int argc, char **argv) {
    struct globals globals = {0};  // Zero-initialize the globals struct

    // Connect to the Wayland display server
    globals.display = wl_display_connect(NULL);
    if (!globals.display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        exit(EXIT_FAILURE);
    } else {
        fprintf(stderr, "Connected to Wayland display successfully\n");
    }

    // Get the registry and set up the registry listener
    struct wl_registry *registry = wl_display_get_registry(globals.display);
    wl_registry_add_listener(registry, &registry_listener, &globals);
    /******************************************************************************
     * 
     * @WAYLAND_ROUND_TRIP:
     * - Sends all pending requests from the client to the server.
     * - Waits for the server to process those requests.
     * - Receives and processes all corresponding events from the server.
     *
     * Why it's needed:
     * - Wayland communication is asynchronous: requests are sent, but the client
     *   does not know when the server will process and respond to them.
     * - wl_display_roundtrip ensures that the client waits until the server has 
     *   processed its requests and all related events have been handled.
     * - It synchronizes the client with the server, guaranteeing that critical
     *   resources (like compositor and xdg_wm_base) are available and ready 
     *   before proceeding further.
     *
     * Example use case:
     * - After binding to global objects (e.g., compositor, window manager base),
     *   we need to make sure that these objects are available before trying to
     *   use them. Calling wl_display_roundtrip ensures the server has processed
     *   the bindings and the client has received the necessary events confirming
     *   the bindings were successful.
     *
     *******************************************************************************/ 
    if (wl_display_roundtrip(globals.display) < 0) {
        fprintf(stderr, "Roundtrip failed\n");
        exit(EXIT_FAILURE);
    } else {
        fprintf(stderr, "Roundtrip completed successfully\n");
    }

    // Check if the compositor and wm_base were successfully bound
    if (!globals.compositor) {
        fprintf(stderr, "Failed to bind compositor\n");
        exit(EXIT_FAILURE);
    } else {
        fprintf(stderr, "Compositor bound successfully\n");
    }

    if (!globals.wm_base) {
        fprintf(stderr, "xdg_wm_base is not available\n");
        exit(EXIT_FAILURE);
    } else {
        fprintf(stderr, "xdg_wm_base bound successfully\n");
    }

    // Create a Wayland surface
    globals.surface = wl_compositor_create_surface(globals.compositor);
    if (!globals.surface) {
        fprintf(stderr, "Failed to create Wayland surface\n");
        exit(EXIT_FAILURE);
    }

    // Create an xdg surface
    globals.xdg_surface = xdg_wm_base_get_xdg_surface(globals.wm_base, globals.surface);
    if (!globals.xdg_surface) {
        fprintf(stderr, "Failed to create xdg surface\n");
        exit(EXIT_FAILURE);
    }

    // Create a top-level xdg surface (window)
    globals.xdg_toplevel = xdg_surface_get_toplevel(globals.xdg_surface);
    if (!globals.xdg_toplevel) {
        fprintf(stderr, "Failed to create xdg toplevel\n");
        exit(EXIT_FAILURE);
    }

    // Commit the surface to display it
    wl_surface_commit(globals.surface);

    // Initialize EGL for rendering
    init_egl(&globals);

    // Main rendering loop: render the triangle and handle Wayland events
    int count = 0;
    while (1) {
        int dispatch_result = wl_display_dispatch(globals.display);
        if (dispatch_result == -1) {
            fprintf(stderr, "wl_display_dispatch failed: %s\n", strerror(errno));
            break;  // Exit loop if dispatch fails
        }

        fprintf(stderr, "Before rendering triangle %d\n", count);
        render_triangle();  // Render the triangle
        eglSwapBuffers(egl_display, egl_surface);  // Swap buffers to display it
        fprintf(stderr, "After rendering triangle %d\n", count);
        ++count;
    }

    // Cleanup resources before exit
    if (globals.egl_window) {
        wl_egl_window_destroy(globals.egl_window);
    }
    if (globals.xdg_toplevel) {
        xdg_toplevel_destroy(globals.xdg_toplevel);
    }
    if (globals.xdg_surface) {
        xdg_surface_destroy(globals.xdg_surface);
    }
    if (globals.surface) {
        wl_surface_destroy(globals.surface);
    }
    eglDestroySurface(egl_display, egl_surface);
    eglDestroyContext(egl_display, egl_context);
    eglTerminate(egl_display);
    wl_display_disconnect(globals.display);

    return 0;
}
