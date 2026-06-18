#include "shader.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

static std::string read_file(const char* path){
    std::ifstream f(path);
    if (!f.is_open()){
        std::cerr << "[shader] failed to open: " << path << "\n";
        return "";
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void shader_init(Shader& s, const char* vert_src, const char* frag_src){
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

// loads shader source from disk then forwards to shader_init
// paths are relative the same way texture/obj paths are (eg "../assets/shaders/lit.vert")
void shader_init_from_file(Shader& s, const char* vert_path, const char* frag_path){
    std::string vert_src = read_file(vert_path);
    std::string frag_src = read_file(frag_path);
    if (vert_src.empty() || frag_src.empty()){
        std::cerr << "[shader] missing source: "
                   << vert_path << " / " << frag_path << "\n";
        return;
    }
    shader_init(s, vert_src.c_str(), frag_src.c_str());
}

void shader_destroy(Shader& s){
    glDeleteProgram(s.id);
    s.id = 0;
}

void shader_bind(const Shader& s){
    glUseProgram(s.id);
}

void shader_set_mat4(const Shader& s, const char* name, const float* value){
    glUniformMatrix4fv(glGetUniformLocation(s.id, name), 1, GL_FALSE, value);
}

void shader_set_vec3(const Shader& s, const char* name, float x, float y, float z){
    glUniform3f(glGetUniformLocation(s.id, name), x, y, z);
}