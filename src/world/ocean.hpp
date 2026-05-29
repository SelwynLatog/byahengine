#pragma once
#include "../renderer/mesh.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

// a rectangular ocean zone placed in the editor
// TODO: make it non flat mesh y non fixed
struct OceanZone {
    int id = -1;
    float x_min = 0, x_max = 0;
    float z_min = 0, z_max = 0;
    float y_level = -0.4f; // world Y of this zone's surface

    Mesh mesh;
    bool mesh_dirty = true;
};

void ocean_build_mesh(OceanZone& zone);
void ocean_destroy(OceanZone& zone);
void ocean_zones_save(const std::vector<OceanZone>& zones, const std::string& path);
bool ocean_zones_load(std::vector<OceanZone>& zones, const std::string& path);