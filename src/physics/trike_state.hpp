#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "trike_aabb.hpp"

// full dynamic state of the tricycle
// all values are in SI units (meters, radians, seconds)
// this struct acts as the single source of truth
struct TrikeState{
    
    glm::vec3 position = glm::vec3(0.0f); //world position of rear axle, ground level
    float heading = 0.0f; // yaw in radians, 0 = facing +X axis
    float pitch_angle = 0.0f; // body pitch radians positive means nose up
    glm::vec3 surface_normal = glm::vec3(0.0f, 1.0f, 0.0f); // ground normal under trike
    float slope_force = 0.0f; // gravity component along slope, m/s^2. negative = uphill drag
    float speed = 0.0f; // signed scalar m/s, positive = forward longitudinal
    float steer_angle = 0.0f; // current float wheel angle, radians
    float lateral_speed= 0.0f;
    float velocity_heading= 0.0f; // direction trike is actually moving lags behind body heading
    
    // roll dynamics
    float roll_angle= 0.0f; // body roll, radians.positive= rolling right
    float roll_rate= 0.0f; // roll angular velocity, radians/sec
    
    // rollover state
    bool is_rolled_over= false; // true means trike has tipped, physics is frozen
    bool is_tipping= false; // init goofy rollover state when true
    glm::vec3 tumble_vel  = glm::vec3(0.0f); // velocity during barrel roll tumble
    float rollover_timer= 0.0f; // count up after tip, respawn at threshold

    // impact state
    // set on collision
    // used by response + cam shake + HUD
    float last_impact_force= 0.0f; // magnitude of velocity the moment of impact
    float impact_timer= 0.0f; // counts down after impact, drives flash/shake

    // suspension
    float susp_offset = 0.0f; // spring travel in meters
    float susp_vel = 0.0f; // suspension velocity m/s
    float body_bob = 0.0f; // idle engine vibration which is just small sinusoidal Y offset

    // world-space AABB
    // used for collision detection against obstacles and world bounds
    Aabb aabb;
    
};