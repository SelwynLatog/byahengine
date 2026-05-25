#pragma once
#include "height_field.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string>

// road surface types same as RoadDef table
enum RoadType {
    ROAD_ASPHALT = 0,
    ROAD_GRAVEL,
    ROAD_DIRT,
    ROAD_SAND,
    ROAD_GRASS,
    ROAD_CEMENT,
    ROAD_COUNT
};

// a single road spline
// control points are full vec3
struct RoadSpline {
    int id = -1;
    float width = 7.0f;
    RoadType type = ROAD_ASPHALT;

    std::vector<glm::vec3> points;  // world-space control points

    // GPU mesh
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
    int index_count = 0;
};

// build (or rebuild) the triangle mesh for a spline
// walks the point list, extrudes width-wide quads perpendicular to each segment
void road_spline_build_mesh(RoadSpline& road, const HeightField* hf = nullptr);

// free GPU buffers
void road_spline_destroy(RoadSpline& road);

// save/load
void road_splines_save(const std::vector<RoadSpline>& roads, const std::string& path);
bool road_splines_load(std::vector<RoadSpline>& roads, const std::string& path);