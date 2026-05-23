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

    // Catmull-Rom subdivision
    // expand the sparse control points into a dense smooth curve
    // each segment between two control points is subdivided into STEPS smaller steps
    // phantom points at both ends mirror the first/last segment so the curve
    static const int STEPS = 12; // subdivisions per segment - higher means smoother

    auto catmull_rom = [](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t) -> glm::vec3 {
        // standard Catmull-Rom formula, alpha=0.5 (centripetal)
        float t2 = t * t;
        float t3 = t2 * t;
        return 0.5f * (
            (2.0f * p1) +
            (-p0 + p2) * t +
            (2.0f*p0 - 5.0f*p1 + 4.0f*p2 - p3) * t2 +
            (-p0 + 3.0f*p1 - 3.0f*p2 + p3) * t3
        );
    };

    // build the smooth point list from the control points
    std::vector<glm::vec3> smooth;
    int n = (int)road.points.size();

    for (int i = 0; i < n - 1; i++){
        // phantom endpoints mirror the curve at the ends
        glm::vec3 p0 = (i == 0)
            ? road.points[0] - (road.points[1] - road.points[0])
            : road.points[i - 1];
        glm::vec3 p1 = road.points[i];
        glm::vec3 p2 = road.points[i + 1];
        glm::vec3 p3 = (i + 2 >= n)
            ? road.points[n-1] + (road.points[n-1] - road.points[n-2])
            : road.points[i + 2];

        for (int s = 0; s < STEPS; s++){
            float t = (float)s / (float)STEPS;
            smooth.push_back(catmull_rom(p0, p1, p2, p3, t));
        }
    }

    smooth.push_back(road.points[n - 1]);

    std::vector<RoadVertex> verts;
    std::vector<unsigned int> indices;

    int m = (int)smooth.size();
    float half_w = road.width * 0.5f;
    float v_acc  = 0.0f;

    for (int i = 0; i < m; i++){
        glm::vec3 tangent;
        if (i == 0)
            tangent = glm::normalize(smooth[1] - smooth[0]);
        else if (i == m - 1)
            tangent = glm::normalize(smooth[m-1] - smooth[m-2]);
        else
            tangent = glm::normalize(smooth[i+1] - smooth[i-1]);

        glm::vec3 world_up = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 right    = glm::normalize(glm::cross(tangent, world_up));
        glm::vec3 normal   = glm::normalize(glm::cross(right, tangent));

        glm::vec3 left_pos  = smooth[i] - right * half_w;
        glm::vec3 right_pos = smooth[i] + right * half_w;

        if (i > 0){
            float seg_len = glm::length(smooth[i] - smooth[i-1]);
            v_acc += seg_len / road.width;
        }

        RoadVertex vL, vR;
        vL.pos = left_pos;  vL.normal = normal; vL.uv = glm::vec2(0.0f, v_acc);
        vR.pos = right_pos; vR.normal = normal; vR.uv = glm::vec2(1.0f, v_acc);
        verts.push_back(vL);
        verts.push_back(vR);
    }

    for (int i = 0; i < m - 1; i++){
        unsigned int bl = i * 2 + 0;
        unsigned int br = i * 2 + 1;
        unsigned int tl = i * 2 + 2;
        unsigned int tr = i * 2 + 3;

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