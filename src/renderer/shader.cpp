#include "shader.hpp"
#include <iostream>

void shader_init(Shader& s, const char* vert_src, const char* frag_src)
{
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vert_src, nullptr);
    glCompileShader(vert);
    {
        int ok; char log[1024];
        glGetShaderiv(vert, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(vert, 1024, nullptr, log);
            std::cerr << "[shader] vert error:\n" << log << "\n";
        } 
        else {
            std::cerr << "[shader] vert OK\n";
        }
    }

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &frag_src, nullptr);
    glCompileShader(frag);
    {
        int ok; char log[1024];
        glGetShaderiv(frag, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(frag, 1024, nullptr, log);
            std::cerr << "[shader] frag error:\n" << log << "\n";
        } 
        else {
            std::cerr << "[shader] frag OK\n";
        }
    }

    s.id = glCreateProgram();
    glAttachShader(s.id, vert);
    glAttachShader(s.id, frag);
    glLinkProgram(s.id);
    {
        int ok; char log[1024]; GLsizei len;
        glGetProgramiv(s.id, GL_LINK_STATUS, &ok);
        if (!ok) {
            glGetProgramInfoLog(s.id, 1024, &len, log);
            std::cerr << "[shader] link error:\n" << log << "\n";
        } 
        else {
            std::cerr << "[shader] program linked OK\n";
        }
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
}

void shader_destroy(Shader& s)
{
    glDeleteProgram(s.id);
    s.id = 0;
}

void shader_bind(const Shader& s)
{
    glUseProgram(s.id);
}

void shader_set_mat4(const Shader& s, const char* name, const float* value)
{
    glUniformMatrix4fv(glGetUniformLocation(s.id, name), 1, GL_FALSE, value);
}

void shader_set_vec3(const Shader& s, const char* name, float x, float y, float z)
{
    glUniform3f(glGetUniformLocation(s.id, name), x, y, z);
}