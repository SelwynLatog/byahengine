#pragma once
#include "trike_state.hpp"

// driving input, all values normalised to [-1, 1] or [0,1]
// produced by the input layer, consumed by trike_physics_update()

struct TrikeInput {

    float throttle = 0.0f; //[0,1] engine force scalar
    float brake = 0.0f; //[0,1] brake force scalar
    float steer = 0.0f; //[-1,1] left= negative, right=positive
    float reverse  = 0.0f; //[0,1] reverse intent
};

// advances the trike simulation by dt seconds
// reads input, mutates state. Call once per fixed timestep
void trike_physics_update(TrikeState& state, const TrikeInput& input, float dt);