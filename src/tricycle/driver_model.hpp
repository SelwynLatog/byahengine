#pragma once
#include "../renderer/shader.hpp"
#include "../renderer/obj_mesh.hpp"
#include "../core/player_state.hpp"
#include "../physics/trike_state.hpp"
#include "driver_anim.hpp"
#include <glm/glm.hpp>

struct DriverModel {
    // one mesh per bone part, sliced from the same OBJ load
    ObjMesh parts[BONE_COUNT];
    BonePivot pivots[BONE_COUNT]; // joint pivots in model space

    float model_scale   = 1.0f;
    glm::vec3 model_center = glm::vec3(0.0f); // XZ center for alignment
    float model_foot_z  = 0.0f;  // min Z in model space, for foot anchoring
    float half_height   = 1.0f;
};

void driver_model_init(DriverModel& d);

void driver_model_draw(
    const DriverModel& d,
    const PlayerState& player,
    const TrikeState& trike,
    const Shader& shader,
    const glm::mat4& view,
    const glm::mat4& proj);

// pose editor variant draws driver in sit pose with live euler overrides
// seat_offset: world-space position to place driver (replaces Const seat values)
// euler_deg: per-bone XYZ euler offsets in degrees, added on top of pose_sit()
// highlight_bone: bone index to tint orange, -1 = no tint
void driver_model_draw_pose(
    const DriverModel& d,
    glm::vec3 seat_offset,
    const glm::vec3 euler_deg[6],
    int highlight_bone,
    const Shader& shader,
    const glm::mat4& view,
    const glm::mat4& proj);

void driver_model_destroy(DriverModel& d);