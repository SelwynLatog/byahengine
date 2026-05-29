#include "ocean.hpp"
#include "../core/const.hpp"
#include <glad/glad.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>

void ocean_build_mesh(OceanZone& zone){
    if (zone.mesh.vao) mesh_destroy(zone.mesh);

    float spacing = Const::OCEAN_GRID_SPACING;
    float x0 = zone.x_min, x1 = zone.x_max;
    float z0 = zone.z_min, z1 = zone.z_max;
    float y  = zone.y_level;

    int cols = (int)std::ceil((x1 - x0) / spacing) + 1;
    int rows = (int)std::ceil((z1 - z0) / spacing) + 1;
    if (cols < 2 || rows < 2) return;

    // interleaved: pos(3) + color(3)
    float w = x1 - x0;
    float d = z1 - z0;

    std::vector<float> verts;
    verts.reserve(rows * cols * 6);

    for (int r = 0; r < rows; r++){
        for (int c = 0; c < cols; c++){
            float x = x0 + c * spacing;
            float z = z0 + r * spacing;
            if (x > x1) x = x1;
            if (z > z1) z = z1;

            float edge_x = std::min(x - x0, x1 - x) / (w * 0.5f);
            float edge_z = std::min(z - z0, z1 - z) / (d * 0.5f);
            float depth = std::min(glm::clamp(edge_x, 0.0f, 1.0f),
                                   glm::clamp(edge_z, 0.0f, 1.0f));

            // deep: (0.04, 0.18, 0.42)  shallow: (0.05, 0.52, 0.58)
            float cr = 0.04f + depth * 0.01f;
            float cg = 0.18f + depth * (0.52f - 0.18f);
            float cb = 0.42f + depth * (0.58f - 0.42f);
            cg = 0.52f - depth * (0.52f - 0.18f);
            cb = 0.58f + depth * (0.42f - 0.58f);

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

    glGenVertexArrays(1, &zone.mesh.vao);
    GLuint vbo, ebo;
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(zone.mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size()*sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // color
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    zone.mesh.vbo = vbo;
    zone.mesh.count = (int)indices.size();
    zone.mesh_dirty = false;

    std::cout << "[ocean] built mesh " << (rows*cols) << " verts, "
              << indices.size()/3 << " tris\n";
}

void ocean_destroy(OceanZone& zone){
    if (zone.mesh.vao) mesh_destroy(zone.mesh);
}

void ocean_zones_save(const std::vector<OceanZone>& zones, const std::string& path){
    std::ofstream f(path);
    if (!f){ std::cerr << "ocean_zones: failed to save " << path << "\n"; return; }
    for (const auto& z : zones){
        f << z.id << " "
          << z.x_min << " " << z.x_max << " "
          << z.z_min << " " << z.z_max << " "
          << z.y_level << "\n";
    }
    std::cout << "[ocean] saved " << zones.size() << " zones to " << path << "\n";
}

bool ocean_zones_load(std::vector<OceanZone>& zones, const std::string& path){
    std::ifstream f(path);
    if (!f) return false;
    zones.clear();
    std::string line;
    int max_id = -1;
    while (std::getline(f, line)){
        if (line.empty()) continue;
        std::istringstream ss(line);
        OceanZone z;
        ss >> z.id >> z.x_min >> z.x_max >> z.z_min >> z.z_max >> z.y_level;
        ocean_build_mesh(z);
        zones.push_back(std::move(z));
        if (z.id > max_id) max_id = z.id;
    }
    std::cout << "[ocean] loaded " << zones.size() << " zones from " << path << "\n";
    return true;
}