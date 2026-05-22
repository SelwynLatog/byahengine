#include "road_spline.hpp"
#include <glad/glad.h>
#include <fstream>
#include <iostream>
#include <cmath>

// vertex layout: position (vec3) + normal (vec3) + uv (vec2)
struct RoadVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

void road_spline_build_mesh(RoadSpline& road){
    if (road.points.size() < 2) return;

    std::vector<RoadVertex> verts;
    std::vector<unsigned int> indices;

    int n = (int)road.points.size();
    float half_w = road.width * 0.5f;
    float v_acc  = 0.0f;  // accumulated V coordinate along road length

    for (int i = 0; i < n; i++){
        // tangent: forward direction at this point
        glm::vec3 tangent;
        if (i == 0)
            tangent = glm::normalize(road.points[1] - road.points[0]);
        else if (i == n - 1)
            tangent = glm::normalize(road.points[n-1] - road.points[n-2]);
        else
            // central difference gives a smoother tangent at interior points
            tangent = glm::normalize(road.points[i+1] - road.points[i-1]);

        // road lies on a slope
        // normal is perpendicular to tangent in the XZ plane
        // we derive a local up from the cross of tangent and world right
        // then re-cross to get the true road-surface normal
        glm::vec3 world_up = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(tangent, world_up));
        glm::vec3 normal = glm::normalize(glm::cross(right, tangent));

        // left and right edge vertices
        glm::vec3 left_pos = road.points[i] - right * half_w;
        glm::vec3 right_pos = road.points[i] + right * half_w;

        // accumulate V so texture tiles along road length
        if (i > 0){
            float seg_len = glm::length(road.points[i] - road.points[i-1]);
            v_acc += seg_len / road.width; // 1 tile per road-width length
        }

        RoadVertex vL, vR;
        vL.pos = left_pos; vL.normal = normal; vL.uv = glm::vec2(0.0f, v_acc);
        vR.pos = right_pos; vR.normal = normal; vR.uv = glm::vec2(1.0f, v_acc);
        verts.push_back(vL);
        verts.push_back(vR);
    }

    // build index buffer
    // two triangles per quad between consecutive point pairs
    // winding: CCW from above
    for (int i = 0; i < n - 1; i++){
        unsigned int bl = i * 2 + 0; // bottom-left  (left  edge, current point)
        unsigned int br = i * 2 + 1; // bottom-right (right edge, current point)
        unsigned int tl = i * 2 + 2; // top-left (left  edge, next point)
        unsigned int tr = i * 2 + 3; // top-right (right edge, next point)

        indices.push_back(bl); indices.push_back(br); indices.push_back(tl);
        indices.push_back(br); indices.push_back(tr); indices.push_back(tl);
    }

    road.index_count = (int)indices.size();

    // upload to GPU
    if (road.vao == 0) glGenVertexArrays(1, &road.vao);
    if (road.vbo == 0) glGenBuffers(1, &road.vbo);
    if (road.ebo == 0) glGenBuffers(1, &road.ebo);

    glBindVertexArray(road.vao);

    glBindBuffer(GL_ARRAY_BUFFER, road.vbo);
    glBufferData(GL_ARRAY_BUFFER,
        verts.size() * sizeof(RoadVertex), verts.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, road.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        indices.size() * sizeof(unsigned int), indices.data(), GL_DYNAMIC_DRAW);

    // position: location 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
        (void*)offsetof(RoadVertex, pos));

    // normal: location 1
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
        (void*)offsetof(RoadVertex, normal));

    // uv: location 2
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
        (void*)offsetof(RoadVertex, uv));

    glBindVertexArray(0);
}

void road_spline_destroy(RoadSpline& road){
    if (road.vao) { glDeleteVertexArrays(1, &road.vao); road.vao = 0; }
    if (road.vbo) { glDeleteBuffers(1, &road.vbo); road.vbo = 0; }
    if (road.ebo) { glDeleteBuffers(1, &road.ebo); road.ebo = 0; }
    road.index_count = 0;
}

void road_splines_save(const std::vector<RoadSpline>& roads, const std::string& path){
    std::ofstream f(path);
    if (!f){ std::cerr << "[roads] save failed: " << path << "\n"; return; }

    f << roads.size() << "\n";
    for (const auto& r : roads){
        f << r.id << " " << (int)r.type << " " << r.width << " "
          << r.points.size() << "\n";
        for (const auto& p : r.points)
            f << p.x << " " << p.y << " " << p.z << "\n";
    }
}

bool road_splines_load(std::vector<RoadSpline>& roads, const std::string& path){
    std::ifstream f(path);
    if (!f){ std::cerr << "[roads] load failed: " << path << "\n"; return false; }

    roads.clear();
    int count = 0;
    f >> count;

    for (int i = 0; i < count; i++){
        RoadSpline r;
        int type_int;
        int pt_count;
        f >> r.id >> type_int >> r.width >> pt_count;
        r.type = (RoadType)type_int;

        r.points.resize(pt_count);
        for (auto& p : r.points)
            f >> p.x >> p.y >> p.z;

        road_spline_build_mesh(r);
        roads.push_back(r);
    }
    return true;
}