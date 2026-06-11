#include "height_field.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>

void heightfield_init(HeightField& hf, int rows, int cols, float cell_size, glm::vec3 origin){
    hf.rows = rows;
    hf.cols = cols;
    hf.cell_size = cell_size;
    hf.origin = origin;
    hf.heights.assign(rows * cols, 0.0f);
    hf.surface.assign(rows * cols, (uint8_t)SURFACE_NONE);
}

float heightfield_sample(const HeightField& hf, float x, float z){
    if (hf.rows == 0 || hf.cols == 0) return 0.0f;

    // convert world pos to grid space
    float gx = (x - hf.origin.x) / hf.cell_size;
    float gz = (z - hf.origin.z) / hf.cell_size;

    int c0 = (int)std::floor(gx);
    int r0 = (int)std::floor(gz);
    int c1 = c0 + 1;
    int r1 = r0 + 1;

    c0 = std::clamp(c0, 0, hf.cols - 1);
    c1 = std::clamp(c1, 0, hf.cols - 1);
    r0 = std::clamp(r0, 0, hf.rows - 1);
    r1 = std::clamp(r1, 0, hf.rows - 1);

    // fractional offset within cell [0,1]
    float tx = gx - std::floor(gx);
    float tz = gz - std::floor(gz);

    // bilinear interpolation across the 4 corners
    float h00 = hf.heights[r0 * hf.cols + c0];
    float h10 = hf.heights[r0 * hf.cols + c1];
    float h01 = hf.heights[r1 * hf.cols + c0];
    float h11 = hf.heights[r1 * hf.cols + c1];

    float h0 = h00 + (h10 - h00) * tx;
    float h1 = h01 + (h11 - h01) * tx;
    return h0 + (h1 - h0) * tz;
}

SurfaceType heightfield_get_surface(const HeightField& hf, float x, float z){
    if (hf.surface.empty()) return SURFACE_NONE;
    float lx = x - hf.origin.x;
    float lz = z - hf.origin.z;
    int col = (int)(lx / hf.cell_size);
    int row = (int)(lz / hf.cell_size);
    if (col < 0 || col >= hf.cols || row < 0 || row >= hf.rows) return SURFACE_NONE;
    return (SurfaceType)hf.surface[row * hf.cols + col];
}

glm::vec3 heightfield_normal(const HeightField& hf, float x, float z){
    // give smooth normal that blends across cell boundaries
    float s = hf.cell_size;
    float hL = heightfield_sample(hf, x - s, z);
    float hR = heightfield_sample(hf, x + s, z);
    float hD = heightfield_sample(hf, x, z - s);
    float hU = heightfield_sample(hf, x, z + s);

    // cross product of the two tangent vectors gives the normal
    glm::vec3 tx = glm::vec3(2.0f * s, hR - hL, 0.0f);
    glm::vec3 tz = glm::vec3(0.0f, hU - hD, 2.0f * s);
    return glm::normalize(glm::cross(tz, tx));
}

void heightfield_sculpt(HeightField& hf, float cx, float cz, float radius, float delta){
    // grid-space bounds of affected cells
    float inv = 1.0f / hf.cell_size;
    int c_min = (int)std::floor((cx - radius - hf.origin.x) * inv);
    int c_max = (int)std::ceil ((cx + radius - hf.origin.x) * inv);
    int r_min = (int)std::floor((cz - radius - hf.origin.z) * inv);
    int r_max = (int)std::ceil ((cz + radius - hf.origin.z) * inv);

    c_min = std::clamp(c_min, 0, hf.cols - 1);
    c_max = std::clamp(c_max, 0, hf.cols - 1);
    r_min = std::clamp(r_min, 0, hf.rows - 1);
    r_max = std::clamp(r_max, 0, hf.rows - 1);

    for (int r = r_min; r <= r_max; r++){
        for (int c = c_min; c <= c_max; c++){
            // world position of this cell centre
            float wx = hf.origin.x + (c + 0.5f) * hf.cell_size;
            float wz = hf.origin.z + (r + 0.5f) * hf.cell_size;

            float dist = std::sqrt((wx - cx)*(wx - cx) + (wz - cz)*(wz - cz));
            if (dist >= radius) continue;

            // cosine falloff: full strength at centre, zero at edge
            float t = dist / radius;
            float falloff = 0.5f * (1.0f + std::cos(t * 3.14159265f));

            hf.heights[r * hf.cols + c] += delta * falloff;
        }
    }
}

void heightfield_smooth(HeightField& hf, float cx, float cz, float radius, float strength){
    float inv = 1.0f / hf.cell_size;
    int c_min = (int)std::floor((cx - radius - hf.origin.x) * inv);
    int c_max = (int)std::ceil ((cx + radius - hf.origin.x) * inv);
    int r_min = (int)std::floor((cz - radius - hf.origin.z) * inv);
    int r_max = (int)std::ceil ((cz + radius - hf.origin.z) * inv);

    c_min = std::clamp(c_min, 0, hf.cols - 1);
    c_max = std::clamp(c_max, 0, hf.cols - 1);
    r_min = std::clamp(r_min, 0, hf.rows - 1);
    r_max = std::clamp(r_max, 0, hf.rows - 1);

    // work on a copy so smoothing doesn't feed back into itself this frame
    std::vector<float> scratch = hf.heights;

    for (int r = r_min; r <= r_max; r++){
        for (int c = c_min; c <= c_max; c++){
            float wx = hf.origin.x + (c + 0.5f) * hf.cell_size;
            float wz = hf.origin.z + (r + 0.5f) * hf.cell_size;

            float dist = std::sqrt((wx - cx)*(wx - cx) + (wz - cz)*(wz - cz));
            if (dist >= radius) continue;

            float t = dist / radius;
            float falloff = 0.5f * (1.0f + std::cos(t * 3.14159265f));

            // 4-neighbour average
            int nc = 1;
            float sum = hf.heights[r * hf.cols + c];
            if (r > 0) { sum += hf.heights[(r-1) * hf.cols + c]; nc++; }
            if (r < hf.rows - 1) { sum += hf.heights[(r+1) * hf.cols + c]; nc++; }
            if (c > 0) { sum += hf.heights[r * hf.cols + (c-1)]; nc++; }
            if (c < hf.cols - 1) { sum += hf.heights[r * hf.cols + (c+1)]; nc++; }
            float avg = sum / nc;

            scratch[r * hf.cols + c] = glm::mix(
                hf.heights[r * hf.cols + c], avg, strength * falloff);
        }
    }

    hf.heights = scratch;
}

void heightfield_clamp(HeightField& hf, float min_y, float max_y){
    for (float& h : hf.heights)
        h = std::clamp(h, min_y, max_y);
}

void heightfield_flatten(HeightField& hf){
    std::fill(hf.heights.begin(), hf.heights.end(), 0.0f);
}

void heightfield_paint(HeightField& hf, float cx, float cz, float radius, SurfaceType type){
    float inv = 1.0f / hf.cell_size;
    int c_min = (int)std::floor((cx - radius - hf.origin.x) * inv);
    int c_max = (int)std::ceil ((cx + radius - hf.origin.x) * inv);
    int r_min = (int)std::floor((cz - radius - hf.origin.z) * inv);
    int r_max = (int)std::ceil ((cz + radius - hf.origin.z) * inv);

    c_min = std::clamp(c_min, 0, hf.cols - 1);
    c_max = std::clamp(c_max, 0, hf.cols - 1);
    r_min = std::clamp(r_min, 0, hf.rows - 1);
    r_max = std::clamp(r_max, 0, hf.rows - 1);

    for (int r = r_min; r <= r_max; r++){
        for (int c = c_min; c <= c_max; c++){
            float wx = hf.origin.x + (c + 0.5f) * hf.cell_size;
            float wz = hf.origin.z + (r + 0.5f) * hf.cell_size;
            float dist = std::sqrt((wx - cx)*(wx - cx) + (wz - cz)*(wz - cz));
            if (dist >= radius) continue;
            hf.surface[r * hf.cols + c] = (uint8_t)type;
        }
    }
}

void heightfield_push_undo(HeightField& hf){
    HeightField::UndoFrame frame;
    frame.heights = hf.heights;
    frame.surface = hf.surface;
    hf.undo_stack.push_back(std::move(frame));
    if ((int)hf.undo_stack.size() > HeightField::UNDO_MAX)
        hf.undo_stack.erase(hf.undo_stack.begin());
}

void heightfield_pop_undo(HeightField& hf){
    if (hf.undo_stack.empty()) return;
    hf.heights = hf.undo_stack.back().heights;
    hf.surface = hf.undo_stack.back().surface;
    hf.undo_stack.pop_back();
}

void heightfield_save(const HeightField& hf, const std::string& path){
    std::ofstream f(path);
    if (!f){ std::cerr << "[heightfield] save failed: " << path << "\n"; return; }

    f << hf.rows << " " << hf.cols << " " << hf.cell_size << "\n";
    f << hf.origin.x << " " << hf.origin.y << " " << hf.origin.z << "\n";

    for (int r = 0; r < hf.rows; r++){
        for (int c = 0; c < hf.cols; c++){
            f << hf.heights[r * hf.cols + c];
            if (c < hf.cols - 1) f << " ";
        }
        f << "\n";
    }

    for (int r = 0; r < hf.rows; r++){
        for (int c = 0; c < hf.cols; c++){
            f << (int)hf.surface[r * hf.cols + c];
            if (c < hf.cols - 1) f << " ";
        }
        f << "\n";
    }
}

bool heightfield_load(HeightField& hf, const std::string& path){
    std::ifstream f(path);
    if (!f){ std::cerr << "[heightfield] load failed: " << path << "\n"; return false; }

    float ox, oy, oz;
    f >> hf.rows >> hf.cols >> hf.cell_size >> ox >> oy >> oz;
    hf.origin = glm::vec3(ox, oy, oz);
    hf.heights.resize(hf.rows * hf.cols);
    for (int i = 0; i < hf.rows * hf.cols; i++)
        f >> hf.heights[i];

    hf.surface.assign(hf.rows * hf.cols, (uint8_t)SURFACE_NONE);
    int surf_loaded = 0;
    for (int i = 0; i < hf.rows * hf.cols; i++){
        int v;
        if (!(f >> v)) break;
        hf.surface[i] = (uint8_t)v;
        surf_loaded++;
    }
    if (surf_loaded > 0)
        std::cout << "[heightfield] loaded " << surf_loaded << " surface cells\n";
    else
        std::cout << "[heightfield] no surface data in file, defaulting to grass\n";

    return true;
}