#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

// one material parsed from the .mtl file
struct ObjMaterial {
    std::string name;
    glm::vec3 kd       = {1.0f, 1.0f, 1.0f};
    std::string tex_path = "";
};

// one contiguous slice of the flat vertex buffer sharing a single material
struct ObjGroup {
    std::string mat_name;
    int vertex_start = 0;
    int vertex_count = 0;
};

// one named object part (g / o tag in the OBJ)
// may contain several material sub-groups
// pivot_offset: local-space vector from part origin to geometric center
// used to rotate around the correct point (wheel axle, fork shaft, etc.)
struct ObjPart {
    std::string part_name;
    std::vector<ObjGroup> groups;
    glm::vec3 pivot_offset = glm::vec3(0.0f); // computed at load
};

// the full result of loading one OBJ+MTL pair
struct ObjData {
    // flat interleaved buffer: px py pz nx ny nz u v  (8 floats per vertex)
    std::vector<float> vertices;
    std::vector<ObjMaterial> materials;
    std::vector<ObjPart> parts;    // named parts from g/o tags

    std::vector<ObjGroup> groups;
};

bool obj_load(const std::string& obj_path, ObjData& out);
// returns false if cache is missing or stale (caller should re-parse and write)
bool obj_cache_load(const std::string& obj_path, ObjData& out);
void obj_cache_save(const std::string& obj_path, const ObjData& out);
const ObjMaterial* obj_find_material(const ObjData& data, const std::string& name);

// find a part by name
const ObjPart* obj_find_part(const ObjData& data, const std::string& name);