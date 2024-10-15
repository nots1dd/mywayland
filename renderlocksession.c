#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "xdg-shell-client-protocol.h"
#include "ext-session-lock-client-protocol.h"
#include "ext-session-lock-client-protocol.c"
#include "xdg-shell-client-protocol.c"

// Wayland global variables
struct globals {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_surface *surface;
    struct wl_egl_window *egl_window;
    struct xdg_wm_base *wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct ext_session_lock_manager_v1 *session_lock_manager;
    struct ext_session_lock_v1 *session_lock;
    bool locked;
};

// EGL global variables
EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;

// Simple triangle vertices
static const GLfloat vertices[] = {
    0.0f,  0.5f,
   -0.5f, -0.5f,
    0.5f, -0.5f
};

// Wayland registry handler
static void registry_handler(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    struct globals *globals = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        globals->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
        printf("Compositor bound\n");
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        globals->wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        printf("xdg_wm_base bound\n");
    } else if (strcmp(interface, "ext_session_lock_manager_v1") == 0) {
        globals->session_lock_manager = wl_registry_bind(registry, id, &ext_session_lock_manager_v1_interface, 1);
        printf("Session lock manager bound\n");
    }
}

// Wayland registry remove handler (not used in this example)
static void registry_remover(void *data, struct wl_registry *registry, uint32_t id) {
    // Handle object removal if necessary
}

// The Wayland registry listener
static const struct wl_registry_listener registry_listener = {
    registry_handler,
    registry_remover
};

// Initialize EGL
void init_egl(struct globals *globals) {
    egl_display = eglGetDisplay((EGLNativeDisplayType)globals->display);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        exit(EXIT_FAILURE);
    }

    if (!eglInitialize(egl_display, NULL, NULL)) {
        fprintf(stderr, "Failed to initialize EGL\n");
        exit(EXIT_FAILURE);
    }

    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config;
    EGLint num_configs;
    eglChooseConfig(egl_display, attribs, &config, 1, &num_configs);

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        exit(EXIT_FAILURE);
    }

    egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)globals->egl_window, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Failed to create EGL surface\n");
        exit(EXIT_FAILURE);
    }

    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "Failed to make EGL context current\n");
        exit(EXIT_FAILURE);
    }
}

// Render the triangle using OpenGL ES
void render_triangle() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const char *vertex_shader_source =
        "attribute vec2 position;\n"
        "void main() {\n"
        "    gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n";
    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader(vertex_shader);

    GLint compile_status;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compile_status);
    if (compile_status == GL_FALSE) {
        fprintf(stderr, "Vertex shader compilation failed\n");
        exit(EXIT_FAILURE);
    }

    const char *fragment_shader_source =
        "void main() {\n"
        "    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
        "}\n";
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
    glCompileShader(fragment_shader);

    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compile_status);
    if (compile_status == GL_FALSE) {
        fprintf(stderr, "Fragment shader compilation failed\n");
        exit(EXIT_FAILURE);
    }

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);
    glUseProgram(shader_program);

    GLint position_location = glGetAttribLocation(shader_program, "position");
    glVertexAttribPointer(position_location, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(position_location);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    eglSwapBuffers(egl_display, egl_surface);
}

// Lock the session
void lock_session(struct globals *globals) {
    if (!globals->locked) {
        globals->session_lock = ext_session_lock_manager_v1_lock(globals->session_lock_manager);
        globals->locked = true;
        printf("Session locked.\n");
    }
}

// Unlock the session
void unlock_session(struct globals *globals) {
    if (globals->locked && globals->session_lock) {
        ext_session_lock_v1_destroy(globals->session_lock);
        globals->locked = false;
        printf("Session unlocked.\n");
    }
}

void setup_fullscreen(struct globals *globals) {
    xdg_toplevel_set_fullscreen(globals->xdg_toplevel, NULL); // Use the default output
}

int main(int argc, char **argv) {
    struct globals globals = {0};

    globals.display = wl_display_connect(NULL);
    if (!globals.display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        exit(EXIT_FAILURE);
    }

    struct wl_registry *registry = wl_display_get_registry(globals.display);
    wl_registry_add_listener(registry, &registry_listener, &globals);
    wl_display_roundtrip(globals.display);

    if (!globals.wm_base) {
        fprintf(stderr, "xdg_wm_base is not available in this compositor.\n");
        exit(EXIT_FAILURE);
    }

    globals.surface = wl_compositor_create_surface(globals.compositor);
    if (!globals.surface) {
        fprintf(stderr, "Failed to create Wayland surface\n");
        exit(EXIT_FAILURE);
    }

    globals.xdg_surface = xdg_wm_base_get_xdg_surface(globals.wm_base, globals.surface);
    if (!globals.xdg_surface) {
        fprintf(stderr, "Failed to create xdg surface\n");
        exit(EXIT_FAILURE);
    }

    globals.xdg_toplevel = xdg_surface_get_toplevel(globals.xdg_surface);
    if (!globals.xdg_toplevel) {
        fprintf(stderr, "Failed to create xdg toplevel\n");
        exit(EXIT_FAILURE);
    }

    // Force full screen
    setup_fullscreen(&globals);

    globals.egl_window = wl_egl_window_create(globals.surface, 600, 600);
    init_egl(&globals);

    wl_surface_commit(globals.surface);
    wl_display_flush(globals.display);

    // Lock the session to prevent user interaction
    lock_session(&globals);

    // Main loop
    while (wl_display_dispatch(globals.display) != -1) {
        if (!globals.locked) {
            render_triangle();
        }
    }

    // Clean up
    if (globals.session_lock) {
        ext_session_lock_v1_destroy(globals.session_lock);
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
    if (globals.egl_window) {
        wl_egl_window_destroy(globals.egl_window);
    }
    eglDestroyContext(egl_display, egl_context);
    eglDestroySurface(egl_display, egl_surface);
    eglTerminate(egl_display);
    wl_display_disconnect(globals.display);

    return 0;
}
