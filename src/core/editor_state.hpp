#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "../world/world_object.hpp"

// which transform operation currently in use
enum EditorTool{
    TOOL_TRANSLATE,
    TOOL_ROTATE,
    TOOL_SCALE
};

// editor mode state lives in App
// reset when toggling back to drive mode
struct EditorState{
    bool active = false; // true = editor, false = drive

    // freecam
    glm::vec3 cam_pos = glm::vec3(0.0f, 8.0f, 0.0f);
    float cam_yaw = 0.0f;
    float cam_pitch = 0.0f; // radians clamped to avoid gimbal flip

    std::string selected_model = ""; // filename.obj
    int selected_id = -1;
    glm::vec3 ghost_pos = glm::vec3(0.0f); // pos where next obj will land
    bool placement_valid = false; // ghost snapped to ground

    // active tool
    EditorTool tool = TOOL_TRANSLATE;

    // prop palette
    // scan OBJ files from assets/
    // allows selector to scan which page of lets say for now 0-9 we are on
    std::vector<std::string> prop_list;
    int prop_page = 0;

    // dynamic behavior selector
    // when DYNAMIC, select obj type N key cycles through DYN_PRESETS
    int dyn_preset_index = 0;

    // copy/paste clipboard
    // has_clipboard = false until user copies something
    bool has_clipboard = false;
    WorldObject clipboard; // last copied object, position ignored on paste
};