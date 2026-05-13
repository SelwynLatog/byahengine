#pragma once
#include "shader.hpp"
#include "mesh.hpp"
#include "font.hpp"
#include "../core/editor_state.hpp"
#include "../world/world_map.hpp"
#include <glm/glm.hpp>

// all visual feedback for editor:
// uses same gizmo shader pattern as scene.cpp
struct EditorRenderer{
    Shader shader; // flat color pos + rgb
    Mesh grid; // snap grid built once at init
    Font font; // editor control hud
};

// builds the static grid mesh and compiles the flat shader
void editor_renderer_init(EditorRenderer& er);

// draws three things every editor frame
// 1. snap grid on xz plane
// 2. ghost box at cursor showing where next obj will land
// 3. highlight box around the currently selected object
void editor_renderer_draw( EditorRenderer& er, const EditorState& editor, const WorldMap& map,
    const glm::mat4& view, const glm::mat4& proj);

void editor_renderer_destroy(EditorRenderer& er);