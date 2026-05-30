#pragma once
#include "shader.hpp"
#include "mesh.hpp"
#include "obj_mesh.hpp"
#include "../physics/trike_state.hpp"
#include "../physics/obstacle.hpp"
#include "../tricycle/tricycle_mesh.hpp"
#include "../tricycle/tricycle_model.hpp"
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

    // computed at load time from OBJ bounding box
    glm::vec3 model_center = glm::vec3(0.0f);
    float model_scale = 1.0f;
    float model_half_height = 1.0f;

    // skybox
    Shader sky_shader;
    Mesh sky_quad;
    GLuint sky_tex = 0;
};

void scene_init(SceneState& scene);
void scene_destroy(SceneState& scene);

// draws ground, gizmo, trike, obstacle solids, all AABB wireframes
// view/proj come from app since camera lives there
void scene_draw(
    SceneState& scene,
    const TrikeState& trike,
    const std::vector<Obstacle>& obstacles,
    const glm::mat4& view,
    const glm::mat4& proj,
    bool show_hitboxes = false
);

void scene_draw_sky(SceneState& scene, const glm::mat4& view, const glm::mat4& proj);