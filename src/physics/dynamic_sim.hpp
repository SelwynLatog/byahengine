#pragma once
#include "../physics/trike_aabb.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// runtime simulation state for a DYNAMIC world object
// resets to placed position on map reload, persists through drive session otherwise
struct DynamicSim {
    glm::vec3 position = glm::vec3(0.0f); // simulated world pos
    float yaw = 0.0f; // spin on Y
    float pitch = 0.0f; // tip forward/back on X
    float roll = 0.0f; // tip sideways on Z
    glm::vec3 velocity = glm::vec3(0.0f); // XZ linear velocity
    float yaw_vel = 0.0f; // angular vel Y rad/s
    float pitch_vel = 0.0f; // angular vel X rad/s
    float roll_vel = 0.0f; // angular vel Z rad/s
    bool sleeping = true; // skip integration when nearly still
    float hit_timer = 0.0f; // counts down after impact
    Aabb aabb; // world space, rebuilt each frame when awake
};