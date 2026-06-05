#pragma once
#include <glm/glm.hpp>

enum PlayerMode {
    PLAYER_FOOT,
    PLAYER_DRIVING,
    PLAYER_MOUNTING // brief transition lock, ~0.3s
};

struct PlayerState {
    glm::vec3 pos = glm::vec3(2.0f, 0.0f, 0.0f); // spawn slightly off trike
    float yaw = 0.0f; // radians, matches heading convention
    float visual_yaw = 0.0f; // smoothed yaw for rendering that lerps towards yaw
    float speed = 0.0f; // m/s scalar, for anim
    PlayerMode mode = PLAYER_FOOT;

    float mount_timer = 0.0f; // counts down during MOUNTING transition
    float anim_timer = 0.0f; // drives leg swing cycle
};