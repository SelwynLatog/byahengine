#pragma once
#include "shader.hpp"
#include "mesh.hpp"
#include "font.hpp"
#include "obj_mesh.hpp"
#include "../core/editor_state.hpp"
#include "../world/world_map.hpp"
#include "../world/height_field.hpp"
#include "../world/road_spline.hpp"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <unordered_map>
#include "../physics/dynamic_sim.hpp"

// all visual feedback for editor:
// uses same gizmo shader pattern as scene.cpp
struct EditorRenderer{
    Shader shader; // flat color pos + rgb
    Shader obj_shader; // lit pos + normal
    Mesh grid; // snap grid built once at init
    Font font; // editor control hud

    // load prop meshes keyed by filename eg. "balay.obj"
    std::map<std::string, ObjMesh> prop_cache;

    // GL texture objects keyed by absolute tex path
    // 0 = not loaded yet or failed
    std::map<std::string, GLuint> tex_cache;

    // y offset per prop
    // this sets mesh lowest point to 0 so no spawning below ground level
    std::map<std::string, float> prop_y_offset;

    // world space using half-extents derived from min/max at load time
    // this is used for wireframe boxes and raycast AABB so they match actual mesh
     struct PropBounds {
        glm::vec3 local_min = glm::vec3(0.0f);
        glm::vec3 local_max = glm::vec3(1.0f);
    };
    std::map<std::string, PropBounds> prop_bounds;

    // terrain wireframe mesh
    Mesh terrain_mesh;
    bool terrain_mesh_dirty = true; // set true after any sculpt op

    // road spline shader same attrib layout as Roadvertex
    Shader road_shader;
};

// builds the static grid mesh and compiles the flat shader
void editor_renderer_init(EditorRenderer& er);

// draws:
// 1. snap grid on xz plane
// 2. placed WorldMap objects as colored boxes (color by behavior)
// 3. ghost box at cursor showing where next obj will land
// 4. highlight box around the currently selected object
// 5. prop palette panel (left side)
// 6. status HUD (bottom left)
void editor_renderer_draw(EditorRenderer& er, const EditorState& editor, const WorldMap& map,
    const glm::mat4& view, const glm::mat4& proj);

// draws only placed obj meshes
// called both in editor and drive mode
// flash_map: world_object_id -> hit_timer value (0 = no flash)
// dynamic_sims: when present, DYNAMIC objects render from sim position/angles instead of placed transform
void editor_renderer_draw_props(EditorRenderer& er, const WorldMap& map,
    const glm::mat4& view, const glm::mat4& proj,
    const std::map<int,float>& flash_map = {},
    const std::unordered_map<int, DynamicSim>& dynamic_sims = {});

void editor_renderer_destroy(EditorRenderer& er);

float editor_get_y_floor_offset(EditorRenderer& er, const std::string& filename);

void editor_renderer_build_terrain_mesh(EditorRenderer& er, const HeightField& hf);

void editor_renderer_draw_terrain(EditorRenderer& er, const HeightField& hf, const glm::mat4& view, const glm::mat4& proj);

void editor_renderer_draw_roads(EditorRenderer& er, const std::vector<RoadSpline>& roads, const glm::mat4& view, const glm::mat4& proj);