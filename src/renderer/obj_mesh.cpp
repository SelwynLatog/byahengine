#include "obj_mesh.hpp"
#include <iostream>

void obj_mesh_init(ObjMesh& m, ObjData&& data){
    m.data = std::move(data);
    m.total_vertices = (int)m.data.vertices.size() / 8;

    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);

    glBufferData(GL_ARRAY_BUFFER,
        m.data.vertices.size() * sizeof(float),
        m.data.vertices.data(),
        GL_STATIC_DRAW);

    // attribute 0: position (xyz)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
        8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // attribute 1: normal (xyz)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
        8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // attribute 2: uv (xy)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
        8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    std::cout << "[obj_mesh] uploaded " << m.total_vertices
              << " vertices (" << m.data.groups.size() << " groups)\n";
}

void obj_mesh_destroy(ObjMesh& m){
    glDeleteVertexArrays(1, &m.vao);
    glDeleteBuffers(1, &m.vbo);
    m.vao = m.vbo = 0;
    m.total_vertices = 0;
}

void obj_mesh_draw_group(const ObjMesh& m, int group_index){
    if (group_index < 0 || group_index >= (int)m.data.groups.size()) return;

    const ObjGroup& g = m.data.groups[group_index];
    if (g.vertex_count <= 0) return;

    glBindVertexArray(m.vao);
    glDrawArrays(GL_TRIANGLES, g.vertex_start, g.vertex_count);
    glBindVertexArray(0);
}