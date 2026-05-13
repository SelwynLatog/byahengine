#include "editor_renderer.hpp"
#include "../core/const.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cmath>

// mirrors gizmo shader already in scene.cpp
// pos + rgb layout
// u_model/view/proj are set per draw call
static const char* ER_VERT = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_color;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
out vec3 v_color;
void main(){
    gl_Position = u_proj * u_view * u_model * vec4(a_pos, 1.0);
    v_color = a_color;
}
)";

static const char* ER_FRAG = R"(
#version 330 core
in  vec3 v_color;
out vec4 frag_color;
void main(){
    frag_color = vec4(v_color, 1.0);
}
)";

// internal helpers
static void set_mat4(const Shader& s, const char* n, const glm::mat4& m){
    glUniformMatrix4fv(glGetUniformLocation(s.id, n), 1, GL_FALSE, glm::value_ptr(m));
}

// draws a wireframe box from world space min/max cornders
// then uplaods a throwawau vao.vbo each call & destroys after drawing
// not meant for high frequency draws
// purposely for editor only
static void draw_wire_box(
    const Shader& shader,
    const glm::vec3& mn, const glm::vec3& mx,
    const glm::mat4& view, const glm::mat4& proj,
    glm::vec3 color)
{
    // 8 corners of the box
    glm::vec3 c[8] = {
        {mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},
        {mx.x,mn.y,mx.z},{mn.x,mn.y,mx.z},
        {mn.x,mx.y,mn.z},{mx.x,mx.y,mn.z},
        {mx.x,mx.y,mx.z},{mn.x,mx.y,mx.z},
    };

    // 12 edges as index pairs
    // bottom ring, top ring, 4 vertical pillars
    int e[24] = { 0,1,1,2,2,3,3,0, 4,5,5,6,6,7,7,4, 0,4,1,5,2,6,3,7 };

    // expand into flat pos+rgb buffer for mesh_init
    std::vector<float> verts;
    for (int i = 0; i < 24; ++i){
        glm::vec3 p = c[e[i]];
        verts.insert(verts.end(), {
            p.x, p.y, p.z,
            color.r, color.g, color.b
        });
    }

    // throwaway mesh
    // upload, draw, destroy
    Mesh wire;
    mesh_init(wire, verts);
    shader_bind(shader);
    set_mat4(shader, "u_model", glm::mat4(1.0f));
    set_mat4(shader, "u_view",  view);
    set_mat4(shader, "u_proj",  proj);
    glBindVertexArray(wire.vao);
    glDrawArrays(GL_LINES, 0, wire.count);
    glBindVertexArray(0);
    mesh_destroy(wire);
}

void editor_renderer_init(EditorRenderer& er){
    shader_init(er.shader, ER_VERT, ER_FRAG);
    font_init(er.font, Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT);

    // buid the snap grid as a static mesh
    // two sets parallel one along x, one along z
    // color is baked into vertex buffer so no need to uniform per line
    // sprinkle different gray so it wont overpower placed objects
    std::vector<float> lines;
    float r = (float) Const::EDITOR_GRID_RADIUS;

    for (int i = -Const::EDITOR_GRID_RADIUS; i <= Const::EDITOR_GRID_RADIUS; i++){
        float f = (float)i;

        // line along X axis at Z=f
        lines.insert(lines.end(), { -r, 0.0f, f,  0.3f, 0.3f, 0.3f });
        lines.insert(lines.end(), {  r, 0.0f, f,  0.3f, 0.3f, 0.3f });
        // line along Z axis at X=f
        lines.insert(lines.end(), {  f, 0.0f,-r,  0.3f, 0.3f, 0.3f });
        lines.insert(lines.end(), {  f, 0.0f, r,  0.3f, 0.3f, 0.3f });
    }

    mesh_init(er.grid, lines);
}

void editor_renderer_draw( EditorRenderer& er, const EditorState& editor, const WorldMap& map,
    const glm::mat4& view, const glm::mat4& proj){

    // bind shader and set shared uniforms once
    // model is identity for grid
    // each box override per draw_wire_box call
    shader_bind(er.shader);
    set_mat4(er.shader, "u_model", glm::mat4(1.0f));
    set_mat4(er.shader, "u_view", view);
    set_mat4(er.shader, "u_proj", proj);

    // draw snap grid
    // static mesh
    glBindVertexArray(er.grid.vao);
    glDrawArrays(GL_LINES, 0, er.grid.count);
    glBindVertexArray(0);
    
    // ghost box
    // only drawn when cursor is over valid ground and a model is selected
    if (editor.placement_valid && !editor.selected_model.empty()){
        glm::vec3 gp = editor.ghost_pos;
        // for now hardcoded to 0.5 x 1.0 x 0.5
        // dont have actual model dimensions at placement time yet
        // have to download assets first then I change this to pull real model bounds
        draw_wire_box(er.shader,
            gp + glm::vec3(-0.5f, 0.0f, -0.5f),
            gp + glm::vec3( 0.5f, 1.0f,  0.5f),
            view, proj,
            {0.0f, 1.0f, 1.0f});
    }

    // selection highlight
    // wraps curr selected placed obj
    // walks the object list to find the selected id
    if (editor.selected_id != -1){
        for (const auto & o : map.objects){
            if (o.id != editor.selected_id) continue;
            glm::vec3 half = o.scale * 0.5f;
            draw_wire_box(er.shader,
                o.position + glm::vec3(-half.x, 0.0f, -half.z),
                o.position + glm::vec3( half.x, o.scale.y,  half.z),
                view, proj, {1.0f, 0.55f, 0.0f});
            break;
        }
    }
}

void editor_renderer_destroy(EditorRenderer& er){
    shader_destroy(er.shader);
    mesh_destroy(er.grid);
    font_destroy(er.font);
}