#pragma once
#include "../renderer/shader.hpp"
#include "../renderer/obj_mesh.hpp"
#include "../physics/trike_state.hpp"
#include <glm/glm.hpp>

struct TrikeModel {
    ObjMesh mesh;
    float wheel_spin = 0.0f;
};

void trike_model_init(TrikeModel& t);

void trike_model_update(TrikeModel& t, float speed, float dt);

void trike_model_draw(
    const TrikeModel& t,
    const TrikeState& trike,
    const Shader& shader,
    const glm::mat4& view,
    const glm::mat4& proj);

void trike_model_destroy(TrikeModel& t);