#pragma once
#include "editor_state.hpp"
#include "../world/world_map.hpp"
#include "../world/light_source.hpp"
#include "../world/height_field.hpp"
#include "../world/ocean.hpp"
#include "../renderer/editor_renderer.hpp"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

// scans assets/ for *.obj files and fills editor.prop_list
void editor_scan_props(EditorState& editor, const char* assets_dir);

// handles:
// placement
// deletion
// tool switching
// save/load
void editor_input_update(EditorState& editor, WorldMap& map, EditorRenderer& er,
    GLFWwindow* window, const glm::mat4& view, const glm::mat4& proj,
    int screen_w, int screen_h, float dt, bool& map_dirty);

// casts a ray from screen pixel (mx, my) into the world
// returns the XZ ground plane hit pos y=0
// returns false if ray is parallel to ground
bool editor_raycast_ground( double mx, double my, const glm::mat4& view, const glm::mat4& proj,
    int screen_w, int screen_h, glm::vec3& out_pos,
    const HeightField* terrain = nullptr);

// check if ray hits any placed object's AABB
// uses real mesh bounds from er.prop_bounds when available
// returns id of the closest hit object, -1 if none
int editor_raycast_objects(double mx, double my, const glm::mat4& view, const glm::mat4& proj,
    int screen_w, int screen_h, const WorldMap& map, const EditorRenderer& er, bool prefer_small = false);