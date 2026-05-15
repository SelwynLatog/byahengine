#include "editor_renderer.hpp"
#include "obj_loader.hpp"
#include "obj_mesh.hpp"
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

// lit shader - pos + normal layout
// mirrors scene.cpp VERT_SRC/ FRAG_SRC
static const char* ER_LIT_VERT = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat3 u_normal_mat;
out vec3 v_world_normal;
out vec3 v_world_pos;
void main(){
    vec4 world = u_model * vec4(a_pos, 1.0);
    gl_Position = u_proj * u_view * world;
    v_world_normal = normalize(u_normal_mat * a_normal);
    v_world_pos = world.xyz;
}
)";

static const char* ER_LIT_FRAG = R"(
#version 330 core
in vec3 v_world_normal;
in vec3 v_world_pos;
out vec4 frag_color;
uniform vec3 u_kd;
uniform vec3 u_light_dir;
void main(){
    float diff = max(dot(normalize(v_world_normal), u_light_dir), 0.0);
    float ambient = 0.55;
    vec3 lit = u_kd * (ambient + diff * 0.85);
    frag_color = vec4(lit, 1.0);
}
)";

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
    for (int i = 0; i < 24; i++){
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
    shader_init(er.shader,     ER_VERT,     ER_FRAG);
    shader_init(er.obj_shader, ER_LIT_VERT, ER_LIT_FRAG);
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
// returns a ref to the cached ObjMesh for this filename
// loads from assets/ on first call then returns cached entry after
// asset_dir is relative path eg "../assets"

static ObjMesh& get_prop_mesh(EditorRenderer& er, const std::string& filename){
    auto it = er.prop_cache.find(filename);
    if (it != er.prop_cache.end()) return it->second;

    std::string full_path = std::string("../assets/") + filename;
    ObjData data;
    if (!obj_load(full_path, data)){
        er.prop_cache[filename] = ObjMesh{};
        return er.prop_cache[filename];
    }

    // compute y min from vertex buffer before uploading
    // layout is px py pz nx ny nz so Y is at every 6 floats
    float x_min= 1e9f, x_max=-1e9f;
    float y_min= 1e9f, y_max=-1e9f;
    float z_min= 1e9f, z_max=-1e9f;
    for (int i = 0; i < (int)data.vertices.size(); i += 6){
        float x = data.vertices[i];
        float y = data.vertices[i+1];
        float z = data.vertices[i+2];
        x_min=std::min(x_min,x); x_max=std::max(x_max,x);
        y_min=std::min(y_min,y); y_max=std::max(y_max,y);
        z_min=std::min(z_min,z); z_max=std::max(z_max,z);
    }

    // store offset so model matrix can push mesh up to y = 0
    er.prop_y_offset[filename] = (y_min < 1e9f) ? -y_min : 0.0f;

    EditorRenderer::PropBounds bounds;
    bounds.local_min = glm::vec3(x_min, y_min, z_min);
    bounds.local_max = glm::vec3(x_max, y_max, z_max);
    er.prop_bounds[filename] = bounds;

    ObjMesh mesh{};
    obj_mesh_init(mesh, std::move(data));
    er.prop_cache[filename] = std::move(mesh);
    return er.prop_cache[filename];
}

// returns the y floor offset for a given prop filename
float editor_get_y_floor_offset(EditorRenderer& er, const std::string& filename){
    get_prop_mesh(er, filename); // ensure cached
    auto it = er.prop_y_offset.find(filename);
    return (it != er.prop_y_offset.end()) ? it->second : 0.0f;
}

// maps out a color based on obj behavior
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

    // light dir which matches scene.cpp
    static const glm::vec3 LIGHT_DIR = glm::normalize(
        glm::vec3(Const::LIGHT_DIR_X, Const::LIGHT_DIR_Y, Const::LIGHT_DIR_Z));

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

        auto bit = er.prop_bounds.find(o.model_path);
        if (bit != er.prop_bounds.end()){
            // scale local min/max by object scale and offset by position
            // y is also shifted by y_floor_offset to match the model matrix
            float yoff = er.prop_y_offset.count(o.model_path)
                ? er.prop_y_offset[o.model_path] : 0.0f;
            glm::vec3 lmin = bit->second.local_min;
            glm::vec3 lmax = bit->second.local_max;
            glm::vec3 wmin = o.position + glm::vec3(
                lmin.x * o.scale.x,
                (lmin.y + yoff) * o.scale.y,
                lmin.z * o.scale.z);
            glm::vec3 wmax = o.position + glm::vec3(
                lmax.x * o.scale.x,
                (lmax.y + yoff) * o.scale.y,
                lmax.z * o.scale.z);
            draw_wire_box(er.shader, wmin, wmax, view, proj, behavior_color(o.behavior));
        } else {
            // fallback to a unit cube until mesh is cached
            glm::vec3 half = o.scale * 0.5f;
            draw_wire_box(er.shader,
                o.position + glm::vec3(-half.x, 0.0f, -half.z),
                o.position + glm::vec3( half.x, o.scale.y, half.z),
                view, proj, behavior_color(o.behavior));
        }
    }
    // draw placed object meshes
    // bind lit shader once, then loop all placed objects
    // build model matrix from pos/rot/scale per object
    // draw each material group with its kd color
    shader_bind(er.obj_shader);
    set_mat4(er.obj_shader, "u_view", view);
    set_mat4(er.obj_shader, "u_proj", proj);
    glUniform3f(glGetUniformLocation(er.obj_shader.id, "u_light_dir"),
        LIGHT_DIR.x, LIGHT_DIR.y, LIGHT_DIR.z);

    for (auto& o : map.objects){
        if (o.model_path.empty()) continue;

        ObjMesh& mesh = get_prop_mesh(er, o.model_path);
        if (mesh.data.vertices.empty()) continue; // load failed andd skip

        // build model matrix: translate, rotate Y, scale
        // same convention as trike: center-bottom origin
        // also added y-offset lifts so mesh lowest vertex sits at y ground level
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, o.position);
        model = glm::rotate(model, o.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::translate(model, glm::vec3(0.0f, o.y_floor_offset, 0.0f));
        model = glm::scale(model, o.scale);

        glm::mat3 normal_mat = glm::mat3(glm::transpose(glm::inverse(model)));

        set_mat4(er.obj_shader, "u_model", model);
        glUniformMatrix3fv(glGetUniformLocation(er.obj_shader.id, "u_normal_mat"),
            1, GL_FALSE, glm::value_ptr(normal_mat));

        // draw each material group with its kd color
        for (int i = 0; i < (int)mesh.data.groups.size(); i++){
            const ObjGroup& grp = mesh.data.groups[i];
            const ObjMaterial* mat = obj_find_material(mesh.data, grp.mat_name);
            glm::vec3 kd = mat ? mat->kd : glm::vec3(0.8f);
            glUniform3f(glGetUniformLocation(er.obj_shader.id, "u_kd"),
                kd.r, kd.g, kd.b);
            obj_mesh_draw_group(mesh, i);
        }
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
        for (const auto& o : map.objects){
            if (o.id != editor.selected_id) continue;
            auto bit = er.prop_bounds.find(o.model_path);
            if (bit != er.prop_bounds.end()){
                float yoff = er.prop_y_offset.count(o.model_path)
                    ? er.prop_y_offset[o.model_path] : 0.0f;
                glm::vec3 lmin = bit->second.local_min;
                glm::vec3 lmax = bit->second.local_max;
                glm::vec3 wmin = o.position + glm::vec3(
                    lmin.x * o.scale.x,
                    (lmin.y + yoff) * o.scale.y,
                    lmin.z * o.scale.z);
                glm::vec3 wmax = o.position + glm::vec3(
                    lmax.x * o.scale.x,
                    (lmax.y + yoff) * o.scale.y,
                    lmax.z * o.scale.z);
                draw_wire_box(er.shader, wmin, wmax, view, proj, {1.0f, 0.55f, 0.0f});
            } else {
                glm::vec3 half = o.scale * 0.5f;
                draw_wire_box(er.shader,
                    o.position + glm::vec3(-half.x, 0.0f, -half.z),
                    o.position + glm::vec3( half.x, o.scale.y, half.z),
                    view, proj, {1.0f, 0.55f, 0.0f});
            }
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
        int bottom = Const::WINDOW_HEIGHT -220;
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
                y += 20;

                // behavior label matches wireframe color
                const char* bname = "STATIC";
                float br = 0.55f, bg = 0.55f, bb = 0.55f;
                switch(o.behavior){
                    case STATIC: bname="STATIC"; br=0.55f; bg=0.55f; bb=0.55f; break;
                    case DYNAMIC: bname="DYNAMIC"; br=0.20f; bg=0.50f; bb=1.00f; break;
                    case DECORATION: bname="DECORATION"; br=0.95f; bg=0.80f; bb=0.10f; break;
                    case PEDESTRIAN: bname="PEDESTRIAN"; br=0.20f; bg=0.85f; bb=0.30f; break;
                }
                snprintf(buf, sizeof(buf), "BEHAVIOR: %s  [B] cycle", bname);
                font_draw(er.font, buf, x, y, 2, br, bg, bb);
                break;
            }
        }
    }
}

void editor_renderer_destroy(EditorRenderer& er){
    shader_destroy(er.shader);
    shader_destroy(er.obj_shader);
    mesh_destroy(er.grid);
    font_destroy(er.font);
    for (auto& [name, mesh] : er.prop_cache)
    obj_mesh_destroy(mesh);
    er.prop_cache.clear();
}