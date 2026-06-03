#pragma once
#include <glm/glm.hpp>
#include <string>

// behavior types defined here
// determines how the objects interact with the physics engine
enum ObjectBehavior {
    // immovable, full AABB collision 
    // eg. concrete walls, houses, posts
    STATIC,

    // movable, lighter collision
    // eg. traffic cones, poles
    DYNAMIC,

    // ragdoll physics
    PEDESTRIAN,

    // no collision impact & purely visual
    DECORATION
};

// a single placed object in the world
// pos is center-bottom
struct WorldObject{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f); // radians for consistency trike physics already use it
    glm::vec3 scale = glm::vec3(1.0f);
    std::string model_path = "";
    ObjectBehavior behavior = STATIC;
    int id = -1; // unique id will be assigned on placement

    float y_floor_offset = 0.0f;

    // RIGID BODY DEFS - DYNAMIC 
    // TEMP: hardcoded at placement
    // tunable in the future
    float mass = 10.0f;
    float restitution = 0.50f;
    float friction = 0.80f;

    // PEDESTRIAN config
    int npc_type = 0; // maps to NpcType enum
    bool npc_can_hail = false;
    glm::vec3 npc_walk_a = glm::vec3(0.0f); // patrol start
    glm::vec3 npc_walk_b = glm::vec3(0.0f); // patrol end
    glm::vec3 npc_drop_point = glm::vec3(0.0f); // passenger destination
    float npc_weight = 60.0f; // kg, affects trike physics when riding configurable in editor
};