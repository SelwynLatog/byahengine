#pragma once
#include "shader.hpp"
#include "mesh.hpp"
#include "font.hpp"
#include "obj_mesh.hpp"
#include "../core/editor_state.hpp"
#include "../world/world_map.hpp"
#include "../world/height_field.hpp"
#include "../world/road_spline.hpp"
#include "../world/ocean.hpp"
#include "../world/npc.hpp"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <unordered_map>
#include "../physics/dynamic_sim.hpp"
#include "../tricycle/driver_model.hpp"
#include "../tricycle/tricycle_model.hpp"

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

    // terrain solid surface mesh, one triangle per half-cell
    // rebuilt when sculpt or paint changes the terrain
    Mesh terrain_surface_mesh;
    bool terrain_surface_dirty = true;
    std::vector<int> terrain_surface_offsets; // vertex start per surface type
    std::vector<int> terrain_surface_counts;  // vertex count per surface type

    // road spline shader
    Shader road_shader;

    // ocean wave shader + time accumulator
    Shader ocean_shader;
    float ocean_time = 0.0f;

    // shadow map recevied from scene
    GLuint shadow_depth_tex = 0;
    glm::mat4 light_space_mat = glm::mat4(1.0f);

    // lighting set each frame from scene_update_daytime output
    glm::vec3 sun_dir = glm::vec3(1,2,1);
    glm::vec3 light_color = glm::vec3(1,1,1);
    float ambient = 0.50f;
    float diff_intensity = 0.85f;

    // depth shader for casting shadows from props
    Shader depth_shader;

    // cached uniform locations
    // obj_shader (draw_props)
    struct {
        GLint view, proj, model, normal_mat;
        GLint light_dir, light_space, shadow_bias;
        GLint ambient, diff_intensity, light_color;
        GLint shadow_map, tex, use_texture, kd;
    } obj_loc;

    // road_shader (draw_roads + draw_terrain_surface)
    struct {
        GLint view, proj, model, normal_mat;
        GLint light_dir, light_space, shadow_bias;
        GLint ambient, diff_intensity, light_color;
        GLint shadow_map, tex, use_texture, kd;
    } road_loc;

    // depth_shader (shadow pass)
    struct {
        GLint light_space, model;
    } depth_loc;

    // obj_shader point light locations
    struct {
        GLint count;
        GLint pos[Const::MAX_POINT_LIGHTS];
        GLint color[Const::MAX_POINT_LIGHTS];
        GLint radius[Const::MAX_POINT_LIGHTS];
        GLint intensity[Const::MAX_POINT_LIGHTS];
    } pt_light_loc;

    Mesh line_batch;
    std::vector<float> line_verts;
    glm::vec3 shadow_cull_center = glm::vec3(0.0f);

    std::vector<LightSource> last_lights;
    float night_factor = 1.0f;
    int pose_npc_id = -1; // set during pose mode to suppress world render of that npc
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
    const glm::mat4& view, const glm::mat4& proj, bool show_hitboxes = false,
    const std::vector<LightSource>& lights = {});

// draws only placed obj meshes
// called both in editor and drive mode
// flash_map: world_object_id -> hit_timer value (0 = no flash)
// dynamic_sims: when present, DYNAMIC objects render from sim position/angles instead of placed transform
void editor_renderer_draw_props(EditorRenderer& er, const WorldMap& map,
    const glm::mat4& view, const glm::mat4& proj,
    const std::map<int,float>& flash_map = {},
    const std::unordered_map<int, DynamicSim>& dynamic_sims = {},
    const std::vector<LightSource>& lights = {},
    bool skip_pedestrians = false);

void editor_renderer_shadow_pass(EditorRenderer& er, const WorldMap& map,
    const glm::mat4& light_space_mat,
    const std::unordered_map<int, DynamicSim>& dynamic_sims = {});
    

void editor_renderer_destroy(EditorRenderer& er);

float editor_get_y_floor_offset(EditorRenderer& er, const std::string& filename);

void editor_renderer_preload_textures(EditorRenderer& er);

void editor_renderer_build_terrain_mesh(EditorRenderer& er, const HeightField& hf);

void editor_renderer_draw_terrain(EditorRenderer& er, const HeightField& hf, const glm::mat4& view, const glm::mat4& proj,
    const glm::vec3& brush_pos, float brush_radius, bool placement_valid);

void editor_renderer_build_terrain_surface(EditorRenderer& er, const HeightField& hf, const Ocean& ocean);
void editor_renderer_draw_terrain_surface(EditorRenderer& er, const HeightField& hf,
    const glm::mat4& view, const glm::mat4& proj, const Ocean& ocean);

void editor_renderer_draw_roads(EditorRenderer& er, const std::vector<RoadSpline>& roads, const glm::mat4& view, const glm::mat4& proj);

void editor_renderer_draw_ocean(EditorRenderer& er, Ocean& ocean, const glm::mat4& view, const glm::mat4& proj, float dt, float terrain_x_min, float terrain_x_max, float terrain_z_min, float terrain_z_max);

void editor_renderer_draw_pose_mode(EditorRenderer& er, const EditorState& editor,
    const DriverModel& driver, const TrikeModel& trike,
    const glm::mat4& view, const glm::mat4& proj,
    const DriverModel* npc_model, const WorldMap& map = WorldMap{});