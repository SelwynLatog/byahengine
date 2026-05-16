#pragma once
#include "trike_aabb.hpp"
#include <glm/glm.hpp>
#include <vector>

// static world obstacle
// immovable box with hitbox and a world position
// position is center-bottom same as trike
// purely to tes collision and impact phase
struct Obstacle{
    glm::vec3 position= glm::vec3(0.0f);
    // half width/ height/ depth in metres
    glm::vec3 half_extents= glm::vec3(0.0f);

    // world space aabb
    // built once at spawn
    Aabb aabb;

    // count down after impact
    float hit_timer= 0.0f;

    int world_id= -1;
};

// call once when spawning
// this fills obstacle.aabb from pos and half_extents
// obstacles are static so this runs only once in init
inline void obstacle_init(Obstacle& o){
    glm::vec3 center= o.position + glm::vec3(0.0f, o.half_extents.y, 0.0f );
    o.aabb.min= center - o.half_extents;
    o.aabb.max= center + o.half_extents;
}

// build and return obstacle in one line
inline Obstacle make_obstacle(glm::vec3 position, glm::vec3 half_extents){
    Obstacle o;
    o.position= position;
    o.half_extents= half_extents;
    obstacle_init(o);
    return o;
}