#pragma once
#include "editor_state.hpp"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

// call once when entering editor mode to reset mouse
void editor_cam_init(GLFWwindow* window);

// call every frame in editor mode
// read RMB drag for look
// WASD for movement
void editor_cam_update(EditorState& editor, GLFWwindow* window, float dt);

// returns the view matrix from curr editor freecam state
glm::mat4 editor_cam_get_view(const EditorState& editor);