#pragma once
#include <glm/glm.hpp>

struct LightSource {
    int id = -1;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 color = glm::vec3(1.0f, 0.85f, 0.5f); // warm streetlight def
    float radius = 12.0f;  // falloff distance in metres
    float intensity = 1.0f;   // peak brightness multiplier
};