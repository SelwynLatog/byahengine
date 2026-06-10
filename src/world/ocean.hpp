#pragma once
#include "../renderer/mesh.hpp"
#include <glm/glm.hpp>
#include <string>

struct Ocean {
    bool enabled = false;
    float y_level = -0.4f;
    float time = 0.0f;
    Mesh mesh;
    bool mesh_dirty = true;
};

void ocean_build_mesh(Ocean& ocean, float x_min, float x_max, float z_min, float z_max);
void ocean_destroy(Ocean& ocean);
void ocean_save(const Ocean& ocean, const std::string& path);
bool ocean_load(Ocean& ocean, const std::string& path);