#pragma once
#include <glad/glad.h>
#include "obj_loader.hpp"

// GPU-side mesh for an OBJ-loaded model
// vertex layout: px py pz nx ny nz  (6 floats, same stride as Mesh)
// but semantics differ: attribute 1 is now a normal, not a color
struct ObjMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    int total_vertices = 0;

    // we keep the parsed data alive so we can slice it per group at draw time
    ObjData data;
};

// uploads the full OBJ vertex buffer to the GPU
void obj_mesh_init(ObjMesh& m, ObjData&& data);
void obj_mesh_destroy(ObjMesh& m);

// draws one material group with the given diffuse color already set as a uniform
void obj_mesh_draw_group(const ObjMesh& m, int group_index);