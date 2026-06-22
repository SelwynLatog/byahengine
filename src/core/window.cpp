#include "window.hpp"
#include "const.hpp"
#include <iostream>

static void cb_framebuffer_size(GLFWwindow*, int width, int height){
    glViewport(0, 0, width, height);
}

void window_init(Window& w, int width, int height, const char* title){
    if (!glfwInit()) {
        std::cerr << "[window] glfwInit failed\n";
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    w.handle = glfwCreateWindow(width, height, title, nullptr, nullptr);
    w.width  = width;
    w.height = height;

    if (!w.handle) {
        std::cerr << "[window] glfwCreateWindow failed\n";
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(w.handle);
    glfwSwapInterval(Const::VSYNC ? 1 : 0);
    glfwSetFramebufferSizeCallback(w.handle, cb_framebuffer_size);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "[window] GLAD init failed\n";
        glfwTerminate();
        return;
    }

    std::cout << "[window] OpenGL " << glGetString(GL_VERSION) << "\n";
    std::cout << "[window] GPU:    " << glGetString(GL_RENDERER) << "\n";
}

void window_show(Window& w){
    glfwShowWindow(w.handle);
}

void window_destroy(Window& w){
    glfwDestroyWindow(w.handle);
    glfwTerminate();
    w.handle = nullptr;
}

bool window_should_close(const Window& w){
    return glfwWindowShouldClose(w.handle);
}

void window_swap_buffers(const Window& w){
    glfwSwapBuffers(w.handle);
}

void window_poll_events(){
    glfwPollEvents();
}