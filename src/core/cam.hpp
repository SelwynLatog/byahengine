#pragma once
#include "app.hpp"
#include <glm/glm.hpp>

// camera state lives here so app.cpp and cam.cpp share it without globals
struct CamState {
    float yaw = 0.0f; // degrees, converted to radians at use
    float pitch = 0.0f;
    float dist = 0.0f;
    glm::vec3 pos = glm::vec3(0.0f);
    bool free_cam = false;
    bool needs_snap = true;
};

// compute view matrix for the current frame
// mutates cam in place lerp, spring-back
// returns the resulting view matrix
glm::mat4 cam_update(CamState& cam, const App& app, float dt, bool arrow_held);

// seed cam to player spawn on first frame so there's no jump
void cam_seed(CamState& cam, const App& app);