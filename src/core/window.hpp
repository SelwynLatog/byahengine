#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>

struct Window{
    GLFWwindow* handle= nullptr;
    int width= 0;
    int height=0;
};

void window_init(Window& w, int width, int height, const char* title);
void window_show(Window& w);
void window_destroy(Window& w);
bool window_should_close(const Window& w);
void window_swap_buffers(const Window& w);
void window_poll_events();