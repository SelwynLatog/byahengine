#include "editor_renderer.hpp"
#include "../core/const.hpp"
#include "../world/world_object.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>

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
// maps out a color based on obj behavior
// makes it easier to read at first glance
static glm::vec3 behavior_color(ObjectBehavior b){
     switch(b){
        case STATIC: return {0.55f, 0.55f, 0.55f}; // grey
        case DYNAMIC: return {0.20f, 0.50f, 1.00f}; // blue
        case PEDESTRIAN: return {0.20f, 0.85f, 0.30f}; // green
        case DECORATION: return {0.95f, 0.80f, 0.10f}; // yellow
        default: return {1.00f, 1.00f, 1.00f};
    }
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

    // 1. draw snap grid
    // static mesh
    glBindVertexArray(er.grid.vao);
    glDrawArrays(GL_LINES, 0, er.grid.count);
    glBindVertexArray(0);
    
    // 2. wireframe box colored by behavior
    // gives visual feedback for every placed object even before OBJ meshes load
    for (const auto& o : map.objects){

        // skip selected 
        // it has its own highlight color below
        if (o.id == editor.selected_id) continue;
        glm::vec3 half = o.scale * 0.5f;
        draw_wire_box(er.shader,
            o.position + glm::vec3(-half.x, 0.0f, -half.z),
            o.position + glm::vec3( half.x, o.scale.y, half.z),
            view, proj, behavior_color(o.behavior));
    }

    // 3. ghost box
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

    // 4. selection highlight
    // wraps curr selected placed obj
    // walks the object list to find the selected id
    if (editor.selected_id != -1){
        for (const auto & o : map.objects){
            if (o.id != editor.selected_id) continue;
            glm::vec3 half = o.scale * 0.5f;
            draw_wire_box(er.shader,
                o.position + glm::vec3(-half.x, 0.0f, -half.z),
                o.position + glm::vec3( half.x, o.scale.y, half.z),
                view, proj, {1.0f, 0.55f, 0.0f});
            break;
        }
    }

    // 5. prop palette HUD
    // shows current page of props and highlights selected model
    {
        const int PAGE_SIZE = Const::EDITOR_PAGE_SIZE;
        int total = (int)editor.prop_list.size();
        int x = 16;
        int y = 60;
        font_draw(er.font, "PROPS", x, y, 2, 0.9f, 0.9f, 0.9f);
        y += 30;

         if (total == 0){
            font_draw(er.font, "no .obj in assets/", x, y, 1, 0.5f, 0.5f, 0.5f);
        }
        else {
            int page_start = editor.prop_page * PAGE_SIZE;
            int page_end   = std::min(page_start + PAGE_SIZE, total);

            for (int i = page_start; i < page_end; i++){
                int slot = i - page_start + 1; // 1-9
                bool active = (editor.prop_list[i] == editor.selected_model);

                // truncate long filenames so they don't overflow the panel
                std::string name = editor.prop_list[i];
                if (name.size() > 20) name = name.substr(0, 18) + "..";

                std::string line = std::to_string(slot) + " " + name;

                if (active)
                    font_draw(er.font, line, x, y, 1, 0.0f, 1.0f, 1.0f); // cyan = selected
                else
                    font_draw(er.font, line, x, y, 1, 0.7f, 0.7f, 0.7f); // grey = idle
                y += 20;
            }

            // page indicator
            int max_page = (total - 1) / PAGE_SIZE;
            std::string page_str = "[ pg " + std::to_string(editor.prop_page + 1)
                + "/" + std::to_string(max_page + 1) + " ]";
            font_draw(er.font, page_str, x, y + 4, 3, 0.5f, 0.5f, 0.5f);
        }
    }

    // 6. status HUD
    // shows:
    // tools
    // selected model
    // object count
    // selected object transform
    {
        int x = 16;
        int bottom = Const::WINDOW_HEIGHT -180;
        int y = bottom;

        // tool name
        const char* tool_str = editor.tool == TOOL_TRANSLATE ? "TRANSLATE" :
            editor.tool == TOOL_ROTATE ? "ROTATE" : "SCALE";
        
        font_draw(er.font, std::string("TOOL: ") + tool_str, x, y, 2, 1.0f, 1.0f, 0.2f);
        y += 20;

        // selected model name
        std::string model_label = "MODEL: " + (editor.selected_model.empty() ? "(none)" : editor.selected_model);
        font_draw(er.font, model_label, x, y, 2, 1.0f, 1.0f, 1.0f);
        y += 20;

        // total object count
        font_draw(er.font, "OBJECTS: " + std::to_string(map.objects.size()), x, y, 2, 0.7f, 0.7f, 0.7f);
        y += 20;

        // selected object transform
        // we only showw when something is selected
        if (editor.selected_id != -1){
            for (const auto& o : map.objects){
                if (o.id != editor.selected_id) continue;

                char buf[128];
                snprintf(buf, sizeof(buf), "POS X:%.1f  Y:%.1f  Z:%.1f",
                    o.position.x, o.position.y, o.position.z);
                font_draw(er.font, buf, x, y, 2, 0.6f, 1.0f, 0.6f);
                y += 20;

                snprintf(buf, sizeof(buf), "ROT Y:%.1f deg",
                    glm::degrees(o.rotation.y));
                font_draw(er.font, buf, x, y, 2, 0.6f, 1.0f, 0.6f);
                y += 20;

                snprintf(buf, sizeof(buf), "SCALE X:%.2f  Y:%.2f  Z:%.2f",
                    o.scale.x, o.scale.y, o.scale.z);
                font_draw(er.font, buf, x, y, 2, 0.6f, 1.0f, 0.6f);
                break;
            }
        }
    }
}

void editor_renderer_destroy(EditorRenderer& er){
    shader_destroy(er.shader);
    mesh_destroy(er.grid);
    font_destroy(er.font);
}