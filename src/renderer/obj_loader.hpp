#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

// one material parsed from the .mtl file
struct ObjMaterial {
    std::string name;
    glm::vec3 kd = {1.0f, 1.0f, 1.0f}; // diffuse color, used when no texture
    std::string tex_path = ""; // absolute path to map_Kd texture
};

// one contiguous group of triangles that share the same material
// indices into the final interleaved vertex buffer
struct ObjGroup {
    std::string mat_name;
    int         vertex_start = 0;   // index of first vertex in the flat buffer
    int         vertex_count = 0;
};

// the full result of loading one OBJ+MTL pair
struct ObjData {
    // flat interleaved buffer: px py pz nx ny nz u v  (8 floats per vertex)
    std::vector<float>       vertices;
    std::vector<ObjMaterial> materials;
    std::vector<ObjGroup>    groups;
};

// loads path/to/file.obj - automatically looks for the .mtl in the same dir
// returns false and prints to stderr if the file cannot be opened
bool obj_load(const std::string& obj_path, ObjData& out);

// helper: find a material by name, returns nullptr if not found
const ObjMaterial* obj_find_material(const ObjData& data, const std::string& name);