#include "../core/const.hpp"
#include "ocean.hpp"
#include <glad/glad.h>
#include <fstream>
#include <iostream>
#include <cmath>

void ocean_build_mesh(Ocean& ocean, float x_min, float x_max, float z_min, float z_max){
    if (ocean.mesh.vao) mesh_destroy(ocean.mesh);

    float spacing = Const::OCEAN_GRID_SPACING;
    float y = ocean.y_level;

    int cols = (int)std::ceil((x_max - x_min) / spacing) + 1;
    int rows = (int)std::ceil((z_max - z_min) / spacing) + 1;
    if (cols < 2 || rows < 2) return;

    float w = x_max - x_min;
    float d = z_max - z_min;

    std::vector<float> verts;
    verts.reserve(rows * cols * 6);

    for (int r = 0; r < rows; r++){
        for (int c = 0; c < cols; c++){
            float x = x_min + c * spacing;
            float z = z_min + r * spacing;
            if (x > x_max) x = x_max;
            if (z > z_max) z = z_max;

            float edge_x = std::min(x - x_min, x_max - x) / (w * 0.5f);
            float edge_z = std::min(z - z_min, z_max - z) / (d * 0.5f);
            float depth = std::min(glm::clamp(edge_x, 0.0f, 1.0f),
                                   glm::clamp(edge_z, 0.0f, 1.0f));

            float cr = 0.04f + depth * 0.01f;
            float cg = 0.52f - depth * (0.52f - 0.18f);
            float cb = 0.58f + depth * (0.42f - 0.58f);

            verts.insert(verts.end(), { x, y, z, cr, cg, cb });
        }
    }

    std::vector<unsigned int> indices;
    indices.reserve((rows-1)*(cols-1)*6);
    for (int r = 0; r < rows-1; r++){
        for (int c = 0; c < cols-1; c++){
            unsigned int tl = r*cols + c;
            unsigned int tr = tl + 1;
            unsigned int bl = tl + cols;
            unsigned int br = bl + 1;
            indices.insert(indices.end(), { tl, bl, tr, bl, br, tr });
        }
    }

    glGenVertexArrays(1, &ocean.mesh.vao);
    GLuint vbo, ebo;
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(ocean.mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size()*sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    ocean.mesh.vbo = vbo;
    ocean.mesh.count = (int)indices.size();
    ocean.mesh_dirty = false;

    std::cout << "[ocean] built mesh " << rows*cols << " verts\n";
}

void ocean_destroy(Ocean& ocean){
    if (ocean.mesh.vao) mesh_destroy(ocean.mesh);
}

void ocean_save(const Ocean& ocean, const std::string& path){
    std::ofstream f(path);
    if (!f){ std::cerr << "[ocean] failed to save " << path << "\n"; return; }
    f << ocean.enabled << " " << ocean.y_level << "\n";
    std::cout << "[ocean] saved to " << path << "\n";
}

bool ocean_load(Ocean& ocean, const std::string& path){
    std::ifstream f(path);
    if (!f) return false;
    f >> ocean.enabled >> ocean.y_level;
    std::cout << "[ocean] loaded y=" << ocean.y_level << " enabled=" << ocean.enabled << "\n";
    return true;
}