#pragma once
#include <glad/glad.h>

struct Shader {
    GLuint id= 0;
};

void shader_init(Shader& s, const char* vert_src, const char* frag_src);
void shader_init_from_file(Shader& s, const char* vert_path, const char* frag_path);
void shader_destroy(Shader& s);
void shader_bind(const Shader& s);
void shader_set_mat4(const Shader& s, const char* name, const float* value);
void shader_set_vec3(const Shader& s, const char* name, float x, float y, float z);