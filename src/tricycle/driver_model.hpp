#pragma once
#include "../renderer/shader.hpp"
#include "../renderer/obj_mesh.hpp"
#include "../core/player_state.hpp"
#include "../physics/trike_state.hpp"
#include <glm/glm.hpp>

struct DriverModel {
    ObjMesh mesh;
    float model_scale = 1.0f;
    glm::vec3 model_center = glm::vec3(0.0f);
    float half_height = 1.0f;
    float model_foot_z = 0.0f;
};

void driver_model_init(DriverModel& d);

void driver_model_draw(
    const DriverModel& d,
    const PlayerState& player,
    const TrikeState& trike,
    const Shader& shader,
    const glm::mat4& view,
    const glm::mat4& proj);

void driver_model_destroy(DriverModel& d);