#pragma once
#include "shader.hpp"
#include "mesh.hpp"
#include "../core/const.hpp"
#include "../core/player_state.hpp"
#include "obj_mesh.hpp"
#include "../physics/trike_state.hpp"
#include "../physics/obstacle.hpp"
#include "../tricycle/tricycle_mesh.hpp"
#include "../tricycle/tricycle_model.hpp"
#include "../tricycle/driver_model.hpp"
#include "../world/light_source.hpp"
#include <glm/glm.hpp>
#include <vector>

// all GPU-side scene resources headers here
// shaders, meshes, model transform data
struct SceneState {
    Shader shader;
    Shader gizmo_shader;

    Mesh ground;
    Mesh gizmo;

    ObjMesh trike_mesh; // OBJ file
    Mesh proc_mesh; // hard coded mesh
    TrikeModel trike_model; // animated parts model
    DriverModel driver_model; // animated parts driver model

    // computed at load time from OBJ bounding box
    glm::vec3 model_center = glm::vec3(0.0f);
    float model_scale = 1.0f;
    float model_half_height = 1.0f;

    // skybox
    Shader sky_shader;
    Mesh sky_quad;
    GLuint sky_tex = 0;
    GLuint sky_night_tex = 0;

    float day_time = Const::DAY_START_TIME;
    glm::vec3 sun_dir = glm::vec3(1,2,1);
    glm::vec3 light_color = glm::vec3(1,1,1);
    float ambient = 0.50f;
    float diff_intensity = 0.85f;

    // sky tint + flip for current and next period
    glm::vec3 sky_tint_a = glm::vec3(1,1,1);
    glm::vec3 sky_tint_b = glm::vec3(1,1,1);
    int sky_flip_a = 0;
    int sky_flip_b = 0;
    float sky_blend = 0.0f;
    float sky_rain_blend = 0.0f;
    float sky_rain_target = 0.0f;
    GLuint sky_rain_tex = 0;
    int sky_use_night_b = 0;
    float sky_uv_offset = 0.0f;
    // shadow map
    Shader shadow_shader;
    GLuint shadow_fbo = 0;
    GLuint shadow_depth_tex = 0;
    glm::mat4 light_space_mat = glm::mat4(1.0f);

    // cached uniform locations
    struct {
        GLint view, proj, model, normal_mat;
        GLint light_dir, light_color, light_space;
        GLint ambient, diff_intensity, shadow_bias;
        GLint shadow_map, kd, kd_alt;
        GLint checker_scale, use_checker;
        GLint fog_color, fog_near, fog_far, fog_cam_pos;
    } shader_loc;

    struct {
        GLint view, proj, model;
    } gizmo_loc;

    struct {
        GLint inv_view_proj, sky_tex, sky_night_tex, sky_rain_tex;
        GLint tint_a, tint_b, flip_a, flip_b;
        GLint blend, uv_offset, use_night_b;
        GLint rain_blend;
        GLint night_factor;
    } sky_loc;

    struct {
        GLint light_space, model;
    } shadow_loc;

    struct {
        GLint count;
        GLint pos[Const::MAX_POINT_LIGHTS];
        GLint color[Const::MAX_POINT_LIGHTS];
        GLint radius[Const::MAX_POINT_LIGHTS];
        GLint intensity[Const::MAX_POINT_LIGHTS];
    } light_loc;

    float night_factor = 1.0f;

    glm::vec3 fog_color = glm::vec3(0.5f, 0.6f, 0.7f);
    float fog_near = 60.0f;
    float fog_far = 200.0f;
    
    // persistent line batch for hitbox wireframes
    Mesh line_batch;
    std::vector<float> line_verts;

};

void scene_init(SceneState& scene);
void scene_destroy(SceneState& scene);

// draws ground, gizmo, trike, obstacle solids, all AABB wireframes
// view/proj come from app since camera lives there
void scene_draw(
    SceneState& scene,
    const TrikeState& trike,
    const std::vector<Obstacle>& obstacles,
    const std::vector<LightSource>& lights,
    const glm::mat4& view,
    const glm::mat4& proj,
    bool show_hitboxes = false
);

void scene_draw_sky(SceneState& scene, const glm::mat4& view, const glm::mat4& proj);

void scene_update_daytime(SceneState& scene, float dt);

void scene_shadow_pass(SceneState& scene, const std::vector<Obstacle>& obstacles, glm::vec3 center);

void scene_trike_shadow_draw(SceneState& scene, const TrikeState& trike);

void scene_draw_drop_marker(SceneState& scene, glm::vec3 pos, float pulse, const glm::mat4& view, const glm::mat4& proj);

void scene_draw_driver(
    SceneState& scene,
    const PlayerState& player,
    const TrikeState& trike,
    const glm::mat4& view,
    const glm::mat4& proj,
    const Shader& lit_shader,
    const glm::quat pose_quats[BONE_COUNT],
    const glm::vec3 pose_offsets[BONE_COUNT],
    glm::vec3 pose_seat);