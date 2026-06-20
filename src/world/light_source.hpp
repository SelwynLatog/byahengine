#pragma once
#include "../core/const.hpp"
#include <glm/glm.hpp>
#include <cmath>

struct LightSource {
    int id = -1;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 color = glm::vec3(1.0f, 0.85f, 0.5f); // warm streetlight def
    float radius = 12.0f; // falloff distance in metres
    float intensity = 1.0f;  // peak brightness multiplier
    // spotlight fields
    // ignored when cos_cutoff <= -1.0 (point light default)
    glm::vec3 spot_dir  = glm::vec3(0.0f, 0.0f, 1.0f);
    float cos_cutoff    = -1.0f; // cos of half-angle. -1 = omni, no cone applied
};

// builds a headlight positioned in front of the trike this frame
// call once per frame in drive mode, push result into frame_lights
inline LightSource trike_headlight(const glm::vec3& trike_pos, float heading){
    float cy = std::cos(heading);
    float sy = std::sin(heading);
    LightSource l;
    l.id = -2;
    l.position  = trike_pos + glm::vec3(cy, 0.0f, sy) * Const::HEADLIGHT_OFFSET_FWD
                + glm::vec3(0.0f, Const::HEADLIGHT_OFFSET_Y, 0.0f);
    l.color = glm::vec3(Const::HEADLIGHT_R, Const::HEADLIGHT_G, Const::HEADLIGHT_B);
    l.radius = Const::HEADLIGHT_RADIUS;
    l.intensity = Const::HEADLIGHT_INTENSITY;
    // aim slightly downward so the beam hits the road ahead, not the horizon
    l.spot_dir = glm::normalize(glm::vec3(cy, -0.04f, sy));
    l.cos_cutoff = std::cos(glm::radians(Const::HEADLIGHT_CONE_DEG));
    return l;
}