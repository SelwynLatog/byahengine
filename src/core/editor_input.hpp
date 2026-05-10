#pragma once
#include "editor_state.hpp"
#include "../world/world_map.hpp"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

// handles:
// placement
// deletion
// tool switching
// save/load
void editor_input_update(EditorState & editor, WorldMap& map, GLFWwindow* window, 
    const glm::mat4& view, const glm::mat4& proj, int screen_w, int screen_h, float dt);

// casts a ray from screen pixel (mx, my) into the world
// returns the XZ ground plane hit pos y=0
// returns false if ray is parallel to ground
bool editor_raycast_ground( double mx, double my, const glm::mat4& view, const glm::mat4& proj,
    int screen_w, int screen_h, glm::vec3& out_pos);
