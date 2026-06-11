#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>

// height grid here
// why y is floor at x,z calls heightfield_sample
struct HeightField{
    std::vector<float> heights; // heihts[row*cols+col]
    int rows = 0;
    int cols = 0;
    float cell_size = 2.0f;
    glm::vec3 origin = glm::vec3(0.0f); // world space corner row=0 col=0

    // per-cell surface type, parallel to heights[]
    std::vector<uint8_t> surface; // SurfaceType cast to uint8_t

    // undo stack
    struct UndoFrame {
        std::vector<float>   heights;
        std::vector<uint8_t> surface;
    };
    std::vector<UndoFrame> undo_stack;
    static constexpr int UNDO_MAX = 32;
};

enum SurfaceType : uint8_t {
    SURFACE_NONE     = 0,
    SURFACE_ASPHALT  = 1,
    SURFACE_GRAVEL   = 2,
    SURFACE_DIRT     = 3,
    SURFACE_SAND     = 4,
    SURFACE_GRASS    = 5,
    SURFACE_CEMENT   = 6,
    SURFACE_ROCK     = 7,
    SURFACE_COUNT    = 8
};

// paint a circular region with a surface type
void heightfield_paint(HeightField& hf, float cx, float cz, float radius, SurfaceType type);

// init flat grid of given dimension centered on world origin
void heightfield_init(HeightField& hf, int rows, int cols, float cell_size, glm::vec3 origin);

float heightfield_sample(const HeightField& hf, float x, float z);

SurfaceType heightfield_get_surface(const HeightField& hf, float x, float z);

glm::vec3 heightfield_normal(const HeightField& hf, float x, float z);

// raise or lower circular region centered at world (cx,cz)
void heightfield_sculpt(HeightField& hf, float cx, float cz, float radius, float delta);

// smooth a circular region
void heightfield_smooth(HeightField& hf, float cx, float cz, float radius, float strength);

// clamp all heights to min_y max_y
void heightfield_clamp(HeightField& hf, float min_y, float max_y);

// reset every cell to y=0
void heightfield_flatten(HeightField& hf);

// snapshot current heights onto the undo stack
void heightfield_push_undo(HeightField& hf);

// restore last snapshot
void heightfield_pop_undo(HeightField& hf);

// save/load
void heightfield_save(const HeightField& hf, const std::string& path);
bool heightfield_load(HeightField& hf, const std::string& path);