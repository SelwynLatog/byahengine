#include "../../vendor/stb/stb_image.h"
#include "../core/const.hpp"
#include "../core/settings.hpp"
#include "../core/map_manager.hpp"
#include "../world/world_object.hpp"
#include "../world/ocean.hpp"
#include "../world/npc.hpp"
#include "editor_renderer.hpp"
#include "obj_loader.hpp"
#include "obj_mesh.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <map>
#include <iostream>
#include <fstream>
#include <filesystem>


/**********************************************************************

EDITOR RENDERER

Responsibilities

- Asset mesh caching
- Texture loading/cache
- Prop bounds generation
- Terrain rendering
- Road rendering
- Ocean rendering
- Shadow rendering
- Debug visualization
- Editor gizmos

**********************************************************************/


// internal helpers
static void set_mat4(const Shader& s, const char* n, const glm::mat4& m){
    glUniformMatrix4fv(glGetUniformLocation(s.id, n), 1, GL_FALSE, glm::value_ptr(m));
}
// TEXTURE CACHE
// loads a texture from disk into GL, caches by path
// returns 0 on failure
// cache format: [uint64 mtime][int32 w][int32 h][w*h*4 bytes RGBA]
static bool tex_cache_load(const std::string& path, int& w, int& h, std::vector<unsigned char>& px){
    namespace fs = std::filesystem;
    std::string cp = path + ".texcache";
    if (!fs::exists(cp) || !fs::exists(path)) return false;
    uint64_t src_mtime = (uint64_t)fs::last_write_time(path).time_since_epoch().count();
    std::ifstream f(cp, std::ios::binary);
    if (!f.is_open()) return false;
    uint64_t cached_mtime = 0;
    f.read((char*)&cached_mtime, 8);
    if (cached_mtime != src_mtime) return false;
    f.read((char*)&w, 4);
    f.read((char*)&h, 4);
    px.resize(w * h * 4);
    f.read((char*)px.data(), px.size());
    return (bool)f;
}

static void tex_cache_save(const std::string& path, int w, int h, const unsigned char* px){
    namespace fs = std::filesystem;
    std::string cp = path + ".texcache";
    std::ofstream f(cp, std::ios::binary);
    if (!f.is_open()) return;
    uint64_t mtime = (uint64_t)fs::last_write_time(path).time_since_epoch().count();
    f.write((char*)&mtime, 8);
    f.write((char*)&w, 4);
    f.write((char*)&h, 4);
    f.write((char*)px, w * h * 4);
}

static GLuint load_texture(EditorRenderer& er, const std::string& path){
    if (path.empty()) return 0;
    auto it = er.tex_cache.find(path);
    if (it != er.tex_cache.end()) return it->second;

    int w = 0, h = 0;
    std::vector<unsigned char> px_buf;
    unsigned char* px = nullptr;
    bool from_cache = tex_cache_load(path, w, h, px_buf);

    if (from_cache){
        px = px_buf.data();
    }
    else {
        stbi_set_flip_vertically_on_load(1);
        int ch;
        px = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!px){
            std::cerr << "[tex] failed to load: " << path << "\n";
            er.tex_cache[path] = 0;
            return 0;
        }
        tex_cache_save(path, w, h, px);
    }

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!from_cache) stbi_image_free(px);
    er.tex_cache[path] = id;
    std::cout << "[tex] " << (from_cache ? "cache hit" : "loaded") << " " << w << "x" << h << " " << path << "\n";
    return id;
}

// draws a wireframe box from world space min/max corners
// then uplaods a throwaway vao.vbo each call & destroys after drajwing
// not meant for high frequency draws
// purposely for editor only
static void push_wire_box(
    std::vector<float>& verts,
    const glm::vec3& mn, const glm::vec3& mx,
    glm::vec3 color){
    glm::vec3 c[8] = {
        {mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},
        {mx.x,mn.y,mx.z},{mn.x,mn.y,mx.z},
        {mn.x,mx.y,mn.z},{mx.x,mx.y,mn.z},
        {mx.x,mx.y,mx.z},{mn.x,mx.y,mx.z},
    };
    int e[24] = { 0,1,1,2,2,3,3,0, 4,5,5,6,6,7,7,4, 0,4,1,5,2,6,3,7 };
    for (int i = 0; i < 24; i++){
        glm::vec3 p = c[e[i]];
        verts.insert(verts.end(), { p.x,p.y,p.z, color.r,color.g,color.b });
    }
}

static void draw_settings_overlay(EditorRenderer& er){
    static GLuint s_vao = 0, s_vbo = 0;
    if (!s_vao){
        float verts[] = {
            -1.f,-1.f,0.f,  0.f,0.f,0.f,
             1.f,-1.f,0.f,  0.f,0.f,0.f,
             1.f, 1.f,0.f,  0.f,0.f,0.f,
            -1.f,-1.f,0.f,  0.f,0.f,0.f,
             1.f, 1.f,0.f,  0.f,0.f,0.f,
            -1.f, 1.f,0.f,  0.f,0.f,0.f,
        };
        glGenVertexArrays(1, &s_vao);
        glGenBuffers(1, &s_vbo);
        glBindVertexArray(s_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    glm::mat4 identity = glm::mat4(1.0f);
    shader_bind(er.shader);
    set_mat4(er.shader, "u_model", identity);
    set_mat4(er.shader, "u_view", identity);
    set_mat4(er.shader, "u_proj", identity);

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA, GL_ONE, GL_ONE);
    glBlendColor(0.f, 0.f, 0.f, 0.60f);
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(s_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendColor(0.f, 0.f, 0.f, 0.f);
    glDisable(GL_BLEND);
}

// flushes er.line_verts to the persistent line_batch and draws it
static void flush_line_batch(EditorRenderer& er, const Shader& shader,
    const glm::mat4& view, const glm::mat4& proj){
    if (er.line_verts.empty()) return;
    glBindBuffer(GL_ARRAY_BUFFER, er.line_batch.vbo);
    glBufferData(GL_ARRAY_BUFFER,
        er.line_verts.size() * sizeof(float),
        er.line_verts.data(), GL_DYNAMIC_DRAW);
    er.line_batch.count = (int)er.line_verts.size() / 6;
    shader_bind(shader);
    set_mat4(shader, "u_model", glm::mat4(1.0f));
    set_mat4(shader, "u_view",  view);
    set_mat4(shader, "u_proj",  proj);
    glBindVertexArray(er.line_batch.vao);
    glDrawArrays(GL_LINES, 0, er.line_batch.count);
    glBindVertexArray(0);
    er.line_verts.clear();
}

// uploads point lights into a lit shader's u_light_pos/color/radius/intensity arrays
// culls by distance to cam_pos, writes only the lights that pass into slots 0..N
// shader must already be bound before calling this
static void upload_point_lights(GLuint shader_id, const std::vector<LightSource>& lights,
    const glm::vec3& cam_pos, float night_factor){
    int active = 0;
    char buf[64];
    if (night_factor >= 0.01f){
        for (int i = 0; i < (int)lights.size() && active < Const::MAX_POINT_LIGHTS; i++){
            glm::vec3 d = lights[i].position - cam_pos;
            if (glm::dot(d, d) > Const::LIGHT_CULL_DIST_SQ) continue;
            snprintf(buf, sizeof(buf), "u_light_pos[%d]", active);
            glUniform3f(glGetUniformLocation(shader_id, buf),
                lights[i].position.x, lights[i].position.y, lights[i].position.z);
            snprintf(buf, sizeof(buf), "u_light_color_pt[%d]", active);
            glUniform3f(glGetUniformLocation(shader_id, buf),
                lights[i].color.r, lights[i].color.g, lights[i].color.b);
            snprintf(buf, sizeof(buf), "u_light_radius[%d]", active);
            glUniform1f(glGetUniformLocation(shader_id, buf), lights[i].radius);
            snprintf(buf, sizeof(buf), "u_light_intensity[%d]", active);
            glUniform1f(glGetUniformLocation(shader_id, buf), lights[i].intensity * night_factor);
            snprintf(buf, sizeof(buf), "u_light_spot_dir[%d]", active);
            glUniform3f(glGetUniformLocation(shader_id, buf), lights[i].spot_dir.x, lights[i].spot_dir.y, lights[i].spot_dir.z);
            snprintf(buf, sizeof(buf), "u_light_cos_cutoff[%d]", active);
            glUniform1f(glGetUniformLocation(shader_id, buf), lights[i].cos_cutoff);
            active++;
        }
    }
    glUniform1i(glGetUniformLocation(shader_id, "u_light_count"), active);
}

void editor_renderer_init(EditorRenderer& er){
    shader_init_from_file(er.shader, "../assets/shaders/gizmo.vert", "../assets/shaders/gizmo.frag");
    shader_init_from_file(er.obj_shader, "../assets/shaders/lit.vert", "../assets/shaders/object.frag");
    shader_init_from_file(er.road_shader, "../assets/shaders/lit.vert", "../assets/shaders/road.frag");
    shader_init_from_file(er.ocean_shader, "../assets/shaders/ocean.vert", "../assets/shaders/ocean.frag");
    font_init(er.font, Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT);
    shader_init_from_file(er.depth_shader, "../assets/shaders/depth.vert", "../assets/shaders/depth.frag");

    // build the snap grid as a static mesh
    // two sets parallel one along x, one along z
    // color is baked into vertex buffer so no need to uniform per line
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


    // CACHED UNIFORMS FOR ALL LOCS ONCE
    auto cache_obj = [&](){
        auto& L = er.obj_loc;
        GLuint id = er.obj_shader.id;
        L.view = glGetUniformLocation(id, "u_view");
        L.proj = glGetUniformLocation(id, "u_proj");
        L.model = glGetUniformLocation(id, "u_model");
        L.normal_mat = glGetUniformLocation(id, "u_normal_mat");
        L.light_dir = glGetUniformLocation(id, "u_light_dir");
        L.light_space = glGetUniformLocation(id, "u_light_space");
        L.shadow_bias = glGetUniformLocation(id, "u_shadow_bias");
        L.ambient = glGetUniformLocation(id, "u_ambient");
        L.diff_intensity = glGetUniformLocation(id, "u_diff_intensity");
        L.light_color = glGetUniformLocation(id, "u_light_color");
        L.shadow_map = glGetUniformLocation(id, "u_shadow_map");
        L.tex = glGetUniformLocation(id, "u_tex");
        L.use_texture = glGetUniformLocation(id, "u_use_texture");
        L.kd = glGetUniformLocation(id, "u_kd");
        L.fog_color = glGetUniformLocation(id, "u_fog_color");
        L.fog_near = glGetUniformLocation(id, "u_fog_near");
        L.fog_far = glGetUniformLocation(id, "u_fog_far");
        L.fog_cam_pos = glGetUniformLocation(id, "u_cam_pos_fog");
    };
    auto cache_road = [&](){
        auto& L = er.road_loc;
        GLuint id = er.road_shader.id;
        L.view = glGetUniformLocation(id, "u_view");
        L.proj = glGetUniformLocation(id, "u_proj");
        L.model = glGetUniformLocation(id, "u_model");
        L.normal_mat = glGetUniformLocation(id, "u_normal_mat");
        L.light_dir = glGetUniformLocation(id, "u_light_dir");
        L.light_space = glGetUniformLocation(id, "u_light_space");
        L.shadow_bias = glGetUniformLocation(id, "u_shadow_bias");
        L.ambient = glGetUniformLocation(id, "u_ambient");
        L.diff_intensity = glGetUniformLocation(id, "u_diff_intensity");
        L.light_color = glGetUniformLocation(id, "u_light_color");
        L.shadow_map = glGetUniformLocation(id, "u_shadow_map");
        L.tex = glGetUniformLocation(id, "u_tex");
        L.use_texture = glGetUniformLocation(id, "u_use_texture");
        L.kd = glGetUniformLocation(id, "u_kd");
        L.fog_color = glGetUniformLocation(id, "u_fog_color");
        L.fog_near = glGetUniformLocation(id, "u_fog_near");
        L.fog_far = glGetUniformLocation(id, "u_fog_far");
        L.fog_cam_pos = glGetUniformLocation(id, "u_cam_pos_fog");
    };
    cache_obj();
    cache_road();

    {
        GLuint id = er.ocean_shader.id;
        auto& OL = er.ocean_loc;
        OL.view = glGetUniformLocation(id, "u_view");
        OL.proj = glGetUniformLocation(id, "u_proj");
        OL.time = glGetUniformLocation(id, "u_time");
        OL.y_level = glGetUniformLocation(id, "u_y_level");
        OL.light_dir = glGetUniformLocation(id, "u_light_dir");
        OL.cam_pos = glGetUniformLocation(id, "u_cam_pos");
        OL.light_color = glGetUniformLocation(id, "u_light_color");
        OL.ambient = glGetUniformLocation(id, "u_ambient");
        OL.diff_intensity = glGetUniformLocation(id, "u_diff_intensity");
    }

    er.depth_loc.light_space = glGetUniformLocation(er.depth_shader.id, "u_light_space");
    er.depth_loc.model = glGetUniformLocation(er.depth_shader.id, "u_model");

    {
        GLuint id = er.obj_shader.id;
        auto& LL = er.pt_light_loc;
        LL.count = glGetUniformLocation(id, "u_light_count");
        char buf[64];
        for (int i = 0; i < Const::MAX_POINT_LIGHTS; i++){
            snprintf(buf, sizeof(buf), "u_light_pos[%d]", i);
            LL.pos[i] = glGetUniformLocation(id, buf);
            snprintf(buf, sizeof(buf), "u_light_color_pt[%d]", i);
            LL.color[i] = glGetUniformLocation(id, buf);
            snprintf(buf, sizeof(buf), "u_light_radius[%d]", i);
            LL.radius[i] = glGetUniformLocation(id, buf);
            snprintf(buf, sizeof(buf), "u_light_intensity[%d]", i);
            LL.intensity[i] = glGetUniformLocation(id, buf);
            snprintf(buf, sizeof(buf), "u_light_spot_dir[%d]", i);
            LL.spot_dir[i] = glGetUniformLocation(id, buf);
            snprintf(buf, sizeof(buf), "u_light_cos_cutoff[%d]", i);
            LL.cos_cutoff[i] = glGetUniformLocation(id, buf);
        }
    }

    // init persistent line batch with empty buffer
    // sized for worst case: 200 wire boxes * 24 verts each
    er.line_verts.reserve(200 * 24 * 6);
    std::vector<float> empty(6, 0.0f);
    mesh_init(er.line_batch, empty);



}

// =====================================================
// PROP CACHE + BOUNDS GENERATION
//
// Loads OBJ assets lazily.
//
// Also computes:
//
// - local bounds
// - floor offset
// - cached AABBs
//
// Used by:
//
// - selection
// - hitboxes
// - collision setup
// - placement
//
// =====================================================
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
    for (int i = 0; i < (int)data.vertices.size(); i += 8){
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

void editor_renderer_preload_textures(EditorRenderer& er){
    for (auto& [name, mesh] : er.prop_cache){
        for (int i = 0; i < (int)mesh.data.groups.size(); i++){
            const ObjGroup& grp = mesh.data.groups[i];
            const ObjMaterial* mat = obj_find_material(mesh.data, grp.mat_name);
            if (mat && !mat->tex_path.empty())
                load_texture(er, mat->tex_path);
        }
    }
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

static void rotated_world_bounds(
    glm::vec3 lmin, glm::vec3 lmax,
    const glm::vec3& pos, float yaw, const glm::vec3& scale, float yoff,
    glm::vec3& out_min, glm::vec3& out_max
){

    // apply scale and y offset to local corners
    glm::vec3 smin = glm::vec3(lmin.x * scale.x, (lmin.y + yoff) * scale.y, lmin.z * scale.z);
    glm::vec3 smax = glm::vec3(lmax.x * scale.x, (lmax.y + yoff) * scale.y, lmax.z * scale.z);

    float c = std::cos(yaw);
    float s = std::sin(yaw);

    out_min = glm::vec3( 1e9f);
    out_max = glm::vec3(-1e9f);

    for (int i = 0; i < 8; i++){
        glm::vec3 corner = {
            (i & 1) ? smax.x : smin.x,
            (i & 2) ? smax.y : smin.y,
            (i & 4) ? smax.z : smin.z,
        };
        glm::vec3 rotated = {
            c * corner.x - s * corner.z,
            corner.y,
            s * corner.x + c * corner.z,
        };
        glm::vec3 world = pos + rotated;
        out_min = glm::min(out_min, world);
        out_max = glm::max(out_max, world);
    }
}


// =====================================================
// MAIN EDITOR RENDER PASS
//
// Draw order:
//
// 1. Grid
// 2. Editor gizmos
// 3. Terrain
// 4. Roads
// 5. Props
// 6. NPCs
// 7. Debug overlays
// 8. UI overlays
// TODO: should probably split to different render_what_is_rendered
// filenames but works for now
// core gameplay renders in scene.cpp, editor in here
// =====================================================

void editor_renderer_draw(EditorRenderer& er, const EditorState& editor, const WorldMap& map,
    const glm::mat4& view, const glm::mat4& proj, bool show_hitboxes,
    const std::vector<LightSource>& lights){

    shader_bind(er.shader);
    set_mat4(er.shader, "u_model", glm::mat4(1.0f));
    set_mat4(er.shader, "u_view", view);
    set_mat4(er.shader, "u_proj", proj);

    // SNAP GRID
    // object mode only
    if (editor.mode == MODE_OBJECT){
        glBindVertexArray(er.grid.vao);
        glDrawArrays(GL_LINES, 0, er.grid.count);
        glBindVertexArray(0);
    }

    // LIGHT MODE
    // wire stems + radius circles
    if (editor.mode == MODE_LIGHT){
        er.line_verts.clear();
        for (const auto& l : map.lights){
            bool selected = (l.id == editor.selected_light_id);
            glm::vec3 col = selected ? glm::vec3(1.0f, 1.0f, 0.0f) : glm::vec3(l.color);
            float ground_y = heightfield_sample(map.terrain, l.position.x, l.position.z);
            er.line_verts.insert(er.line_verts.end(),
                {l.position.x, ground_y, l.position.z, col.r, col.g, col.b});
            er.line_verts.insert(er.line_verts.end(),
                {l.position.x, l.position.y, l.position.z, col.r, col.g, col.b});
            static const int SEGS = 32;
            for (int i = 0; i < SEGS; i++){
                float a0 = (float)i / SEGS * 2.0f * 3.14159265f;
                float a1 = (float)(i + 1) / SEGS * 2.0f * 3.14159265f;
                float x0 = l.position.x + std::cos(a0) * l.radius;
                float z0 = l.position.z + std::sin(a0) * l.radius;
                float x1 = l.position.x + std::cos(a1) * l.radius;
                float z1 = l.position.z + std::sin(a1) * l.radius;
                er.line_verts.insert(er.line_verts.end(), {x0, ground_y + 0.05f, z0, col.r, col.g, col.b});
                er.line_verts.insert(er.line_verts.end(), {x1, ground_y + 0.05f, z1, col.r, col.g, col.b});
            }
        }
        flush_line_batch(er, er.shader, view, proj);
    }

    // AMBIENCE SUB AUDIO MODE
    // radius circles + center crosses
    if (editor.mode == MODE_AMBIENCE){
        er.line_verts.clear();
        static const int SEGS = 40;
        for (int z = 0; z < map.ambience_count; z++){
            const AmbienceZone& zone = map.ambience_zones[z];
            bool selected = (zone.id == editor.selected_zone_id);
            glm::vec3 col = selected
                ? glm::vec3(1.0f, 1.0f, 1.0f)
                : (zone.type == AMBIENCE_NIGHT
                    ? glm::vec3(0.55f, 0.20f, 0.90f)
                    : glm::vec3(0.10f, 0.85f, 0.55f));
            float ground_y = heightfield_sample(map.terrain, zone.pos.x, zone.pos.z) + 0.1f;
            float cs = 0.5f;
            er.line_verts.insert(er.line_verts.end(),
                {zone.pos.x - cs, ground_y, zone.pos.z, col.r, col.g, col.b});
            er.line_verts.insert(er.line_verts.end(),
                {zone.pos.x + cs, ground_y, zone.pos.z, col.r, col.g, col.b});
            er.line_verts.insert(er.line_verts.end(),
                {zone.pos.x, ground_y, zone.pos.z - cs, col.r, col.g, col.b});
            er.line_verts.insert(er.line_verts.end(),
                {zone.pos.x, ground_y, zone.pos.z + cs, col.r, col.g, col.b});
            for (int i = 0; i < SEGS; i++){
                float a0 = (float)i / SEGS * 2.0f * 3.14159265f;
                float a1 = (float)(i + 1) / SEGS * 2.0f * 3.14159265f;
                float x0 = zone.pos.x + std::cos(a0) * zone.radius;
                float z0 = zone.pos.z + std::sin(a0) * zone.radius;
                float x1 = zone.pos.x + std::cos(a1) * zone.radius;
                float z1 = zone.pos.z + std::sin(a1) * zone.radius;
                er.line_verts.insert(er.line_verts.end(), {x0, ground_y, z0, col.r, col.g, col.b});
                er.line_verts.insert(er.line_verts.end(), {x1, ground_y, z1, col.r, col.g, col.b});
            }
        }
        flush_line_batch(er, er.shader, view, proj);
    }

    // wireframe boxes by behavior
    if (show_hitboxes){
        er.line_verts.clear();
        for (const auto& o : map.objects){
            if (o.id == editor.selected_id) continue;
            auto bit = er.prop_bounds.find(o.model_path);
            if (bit != er.prop_bounds.end()){
                float yoff = er.prop_y_offset.count(o.model_path)
                    ? er.prop_y_offset[o.model_path] : 0.0f;
                glm::vec3 wmin, wmax;
                rotated_world_bounds(bit->second.local_min, bit->second.local_max,
                    o.position, o.rotation.y, o.scale, yoff, wmin, wmax);
                push_wire_box(er.line_verts, wmin, wmax, behavior_color(o.behavior));
            }
            else {
                glm::vec3 half = o.scale * 0.5f;
                push_wire_box(er.line_verts,
                    o.position + glm::vec3(-half.x, 0.0f, -half.z),
                    o.position + glm::vec3( half.x, o.scale.y, half.z),
                    behavior_color(o.behavior));
            }
        }
        flush_line_batch(er, er.shader, view, proj);
    }

    // placed prop meshes
    editor_renderer_draw_props(er, map, view, proj, {}, {}, lights);

    // ghost box at cursor
    if (editor.placement_valid && !editor.selected_model.empty()){
        glm::vec3 gp = editor.ghost_pos;
        er.line_verts.clear();
        push_wire_box(er.line_verts,
            gp + glm::vec3(-0.5f, 0.0f, -0.5f),
            gp + glm::vec3( 0.5f, 1.0f,  0.5f),
            {0.0f, 1.0f, 1.0f});
        flush_line_batch(er, er.shader, view, proj);
    }

    // selection highlight
    if (editor.selected_id != -1){
        er.line_verts.clear();
        for (const auto& o : map.objects){
            if (o.id != editor.selected_id) continue;
            auto bit = er.prop_bounds.find(o.model_path);
            if (bit != er.prop_bounds.end()){
                float yoff = er.prop_y_offset.count(o.model_path)
                    ? er.prop_y_offset[o.model_path] : 0.0f;
                glm::vec3 wmin, wmax;
                rotated_world_bounds(bit->second.local_min, bit->second.local_max,
                    o.position, o.rotation.y, o.scale, yoff, wmin, wmax);
                push_wire_box(er.line_verts, wmin, wmax, {1.0f, 0.55f, 0.0f});
            }
            else {
                glm::vec3 half = o.scale * 0.5f;
                push_wire_box(er.line_verts,
                    o.position + glm::vec3(-half.x, 0.0f, -half.z),
                    o.position + glm::vec3( half.x, o.scale.y, half.z),
                    {1.0f, 0.55f, 0.0f});
            }
            break;
        }
        flush_line_batch(er, er.shader, view, proj);
    }

    // ROAD MODE
    // cursor diamond + preview line
    if (editor.mode == MODE_ROAD && editor.placement_valid){
        glm::vec3 p = editor.ghost_pos;
        float s = 0.4f;
        glm::vec3 top = p + glm::vec3( 0,  s,  0);
        glm::vec3 bot = p + glm::vec3( 0, -s,  0);
        glm::vec3 lft = p + glm::vec3(-s,  0,  0);
        glm::vec3 rgt = p + glm::vec3( s,  0,  0);
        glm::vec3 fwd = p + glm::vec3( 0,  0, -s);
        glm::vec3 bck = p + glm::vec3( 0,  0,  s);
        std::vector<float> diamond;
        auto push_edge = [&](glm::vec3 a, glm::vec3 b){
            diamond.insert(diamond.end(), {a.x,a.y,a.z, 0.25f,0.75f,1.00f});
            diamond.insert(diamond.end(), {b.x,b.y,b.z, 0.25f,0.75f,1.00f});
        };
        push_edge(top, lft); push_edge(top, rgt);
        push_edge(top, fwd); push_edge(top, bck);
        push_edge(bot, lft); push_edge(bot, rgt);
        push_edge(bot, fwd); push_edge(bot, bck);
        push_edge(lft, fwd); push_edge(fwd, rgt);
        push_edge(rgt, bck); push_edge(bck, lft);
        Mesh dm;
        mesh_init(dm, diamond);
        shader_bind(er.shader);
        set_mat4(er.shader, "u_model", glm::mat4(1.0f));
        set_mat4(er.shader, "u_view",  view);
        set_mat4(er.shader, "u_proj",  proj);
        glBindVertexArray(dm.vao);
        glDrawArrays(GL_LINES, 0, dm.count);
        glBindVertexArray(0);
        mesh_destroy(dm);
        for (const auto& r : map.roads){
            if (r.id != editor.active_road_id) continue;
            if (r.points.empty()) break;
            glm::vec3 last = r.points.back();
            std::vector<float> preview = {
                last.x, last.y, last.z, 0.25f, 0.75f, 1.00f,
                p.x, p.y, p.z, 0.25f, 0.75f, 1.00f,
            };
            Mesh pm;
            mesh_init(pm, preview);
            glBindVertexArray(pm.vao);
            glDrawArrays(GL_LINES, 0, pm.count);
            glBindVertexArray(0);
            mesh_destroy(pm);
            break;
        }
    }
}


void editor_renderer_shadow_pass(EditorRenderer& er, const WorldMap& map,
    const glm::mat4& light_space_mat,
    const std::unordered_map<int, DynamicSim>& dynamic_sims){
    shader_bind(er.depth_shader);
    glUniformMatrix4fv(er.depth_loc.light_space, 1, GL_FALSE, glm::value_ptr(light_space_mat));
    for (auto& o : map.objects){
        if (o.model_path.empty()) continue;
        if (o.behavior == PEDESTRIAN) continue; // drawn separately as NPC with live position

        // shadow cull
        glm::vec3 diff = o.position - er.shadow_cull_center;
        float shadow_cull_sq = my_settings.prop_cull_dist * my_settings.prop_cull_dist;
        if (glm::dot(diff, diff) > shadow_cull_sq) continue;
        ObjMesh& mesh = get_prop_mesh(er, o.model_path);
        if (mesh.data.vertices.empty()) continue;

        glm::mat4 model = glm::mat4(1.0f);
        auto dit = dynamic_sims.find(o.id);
        if (o.behavior == DYNAMIC && dit != dynamic_sims.end()){
            const DynamicSim& sim = dit->second;
            model = glm::translate(model, sim.position);
            model = glm::rotate(model, sim.yaw + o.rotation.y, glm::vec3(0,1,0));
            model = glm::rotate(model, sim.pitch, glm::vec3(1,0,0));
            model = glm::rotate(model, sim.roll, glm::vec3(0,0,1));
            model = glm::translate(model, glm::vec3(0.0f, o.y_floor_offset, 0.0f));
            model = glm::scale(model, o.scale);
        } 
        else {
            model = glm::translate(model, o.position);
            model = glm::rotate(model, o.rotation.y, glm::vec3(0,1,0));
            model = glm::translate(model, glm::vec3(0.0f, o.y_floor_offset, 0.0f));
            model = glm::scale(model, o.scale);
        }

        glUniformMatrix4fv(er.depth_loc.model, 1, GL_FALSE, glm::value_ptr(model));

        // draw all groups 
        // depth only, no material needed
        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, mesh.data.vertices.size() / 8);
        glBindVertexArray(0);
    }
}


void editor_renderer_draw_props(EditorRenderer& er, const WorldMap& map,
    const glm::mat4& view, const glm::mat4& proj,
    const std::map<int,float>& flash_map,
    const std::unordered_map<int, DynamicSim>& dynamic_sims,
    const std::vector<LightSource>& lights,
    bool skip_pedestrians){

    glm::vec3 LIGHT_DIR = glm::normalize(er.sun_dir);

    auto& OL = er.obj_loc;
    shader_bind(er.obj_shader);
    glUniformMatrix4fv(OL.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(OL.proj, 1, GL_FALSE, glm::value_ptr(proj));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform3f(OL.light_dir, LIGHT_DIR.x, LIGHT_DIR.y, LIGHT_DIR.z);
    glUniformMatrix4fv(OL.light_space, 1, GL_FALSE, glm::value_ptr(er.light_space_mat));
    glUniform3f(OL.fog_color, er.fog_color.r, er.fog_color.g, er.fog_color.b);
    glUniform1f(OL.fog_near, my_settings.render_fog ? er.fog_near : Const::CAM_FAR);
    glUniform1f(OL.fog_far,  my_settings.render_fog ? er.fog_far : Const::CAM_FAR + 1.0f);
    glUniform1f(OL.ambient, er.ambient);
    glUniform1f(OL.diff_intensity, er.diff_intensity);
    glUniform3f(OL.light_color, er.light_color.r, er.light_color.g, er.light_color.b);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, er.shadow_depth_tex);
    glUniform1i(OL.shadow_map, 1);
    glActiveTexture(GL_TEXTURE0);

    // cpu cull: only upload lights within draw distance of camera
    glm::vec3 cam_pos = glm::vec3(glm::inverse(view)[3]);
    int active_lcount = 0;
    glUniform3f(OL.fog_cam_pos, cam_pos.x, cam_pos.y, cam_pos.z);
    if (er.night_factor >= 0.01f){
        for (int i = 0; i < (int)lights.size() && active_lcount < Const::MAX_POINT_LIGHTS; i++){
            glm::vec3 d = lights[i].position - cam_pos;
            float light_cull_sq = my_settings.light_cull_dist * my_settings.light_cull_dist;
            if (glm::dot(d, d) > light_cull_sq) continue;
            // compact into slots 0..active_lcount
            glUniform3f(er.pt_light_loc.pos[active_lcount], lights[i].position.x, lights[i].position.y, lights[i].position.z);
            glUniform3f(er.pt_light_loc.color[active_lcount], lights[i].color.r, lights[i].color.g, lights[i].color.b);
            glUniform1f(er.pt_light_loc.radius[active_lcount], lights[i].radius);
            glUniform1f(er.pt_light_loc.intensity[active_lcount], lights[i].intensity * er.night_factor);
            glUniform3f(er.pt_light_loc.spot_dir[active_lcount], lights[i].spot_dir.x, lights[i].spot_dir.y, lights[i].spot_dir.z);
            glUniform1f(er.pt_light_loc.cos_cutoff[active_lcount], lights[i].cos_cutoff);
            active_lcount++;
        }
    }
    glUniform1i(er.pt_light_loc.count, active_lcount);
 
    // extract camera position from inverse view matrix
    // view is already passed in so no extra cost
    er.last_lights = lights;

    for (auto& o : map.objects){
        if (o.model_path.empty()) continue;
        if (skip_pedestrians && o.behavior == PEDESTRIAN) continue;
        if (o.behavior == PEDESTRIAN && o.id == er.pose_npc_id) continue;
        
        // distance cull
        glm::vec3 diff = o.position - cam_pos;
        float prop_cull_sq = my_settings.prop_cull_dist * my_settings.prop_cull_dist;
        if (glm::dot(diff, diff) > prop_cull_sq) continue;
        ObjMesh& mesh = get_prop_mesh(er, o.model_path);
        if (mesh.data.vertices.empty()) continue;

        glm::mat4 model = glm::mat4(1.0f);

        auto dit = dynamic_sims.find(o.id);


        if (o.behavior == DYNAMIC && dit != dynamic_sims.end()){
            // render from simulated transform = tipping, sliding, spinning
            const DynamicSim& sim = dit->second;
            model = glm::translate(model, sim.position);
            model = glm::rotate(model, sim.yaw + o.rotation.y, glm::vec3(0,1,0));
            model = glm::rotate(model, sim.pitch, glm::vec3(1,0,0));
            model = glm::rotate(model, sim.roll, glm::vec3(0,0,1));
            model = glm::translate(model, glm::vec3(0.0f, o.y_floor_offset, 0.0f));
            model = glm::scale(model, o.scale);
        } 
        else {
            // static/decoration/pedestrian = placed transform
            model = glm::translate(model, o.position);
            model = glm::rotate(model, o.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::translate(model, glm::vec3(0.0f, o.y_floor_offset, 0.0f));
            model = glm::scale(model, o.scale);
        }

        // upper-left 3x3 of model is correct normal mat for uniform scale
        // skips the expensive inverse+transpose
        glm::mat3 normal_mat = glm::mat3(model);

        glUniformMatrix4fv(OL.model,      1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix3fv(OL.normal_mat, 1, GL_FALSE, glm::value_ptr(normal_mat));

        // check if this object is flashing from a hit
        float flash = 0.0f;
        auto fit = flash_map.find(o.id);
        if (fit != flash_map.end())
            flash = glm::clamp(fit->second / 0.35f, 0.0f, 1.0f);

        for (int i = 0; i < (int)mesh.data.groups.size(); i++){
            const ObjGroup& grp = mesh.data.groups[i];
            const ObjMaterial* mat = obj_find_material(mesh.data, grp.mat_name);

            glm::vec3 kd = mat ? mat->kd : glm::vec3(0.8f);
            glm::vec3 hit_color = {0.9f, 0.15f, 0.10f};
            kd = glm::mix(kd, hit_color, flash);
            glUniform3f(OL.kd, kd.r, kd.g, kd.b);

            // bind texture if material has one, else flat color
            GLuint tex = (mat && !mat->tex_path.empty())
                         ? load_texture(er, mat->tex_path) : 0;
            if (tex){
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, tex);
                glUniform1i(OL.tex, 0);
                glUniform1i(OL.use_texture, 1);
            } 
            else {
                glUniform1i(OL.use_texture, 0);
            }

            obj_mesh_draw_group(mesh, i);

            if (tex) glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    glDisable(GL_BLEND);
}

void editor_renderer_build_terrain_mesh(EditorRenderer& er, const HeightField& hf){
    if (hf.rows < 2 || hf.cols < 2) return;

    // wireframe grid that follows the heightfield surface
    // one quad per cell, drawn as GL_LINES
    // color encodes height 
    // low=dark blue, high=bright green, gives instant readability
    std::vector<float> lines;
    lines.reserve(hf.rows * hf.cols * 24); // rough upper bound

    float max_h = *std::max_element(hf.heights.begin(), hf.heights.end());
    float min_h = *std::min_element(hf.heights.begin(), hf.heights.end());
    float range = (max_h - min_h) < 0.01f ? 1.0f : (max_h - min_h);

    auto get_pos = [&](int r, int c) -> glm::vec3 {
        float x = hf.origin.x + c * hf.cell_size;
        float z = hf.origin.z + r * hf.cell_size;
        float y = hf.heights[r * hf.cols + c];
        return glm::vec3(x, y, z);
    };

    auto height_color = [&](float h) -> glm::vec3 {
        float t = (h - min_h) / range; // 0=low, 1=high
        // low = dark teal, high = bright lime
        return glm::mix(glm::vec3(0.10f, 0.35f, 0.45f), glm::vec3(0.30f, 0.90f, 0.25f), t);
    };

    auto push_line = [&](glm::vec3 a, glm::vec3 b){
        glm::vec3 ca = height_color(a.y);
        glm::vec3 cb = height_color(b.y);
        lines.insert(lines.end(), { a.x,a.y,a.z, ca.r,ca.g,ca.b });
        lines.insert(lines.end(), { b.x,b.y,b.z, cb.r,cb.g,cb.b });
    };

    for (int r = 0; r < hf.rows; r++){
        for (int c = 0; c < hf.cols; c++){
            glm::vec3 p = get_pos(r, c);
            // horizontal edge (along X)
            if (c < hf.cols - 1) push_line(p, get_pos(r, c + 1));
            // vertical edge (along Z)
            if (r < hf.rows - 1) push_line(p, get_pos(r + 1, c));
        }
    }

    if (er.terrain_mesh.vao){
        mesh_destroy(er.terrain_mesh);
        er.terrain_mesh.vao = 0;
        er.terrain_mesh.vbo = 0;
        er.terrain_mesh.count = 0;
    }
    mesh_init(er.terrain_mesh, lines);
    er.terrain_mesh_dirty = false;
}

void editor_renderer_draw_terrain(EditorRenderer& er, const HeightField& hf,
    const glm::mat4& view, const glm::mat4& proj,
    const glm::vec3& brush_pos, float brush_radius, bool placement_valid){

    if (er.terrain_mesh_dirty)
        editor_renderer_build_terrain_mesh(er, hf);

    shader_bind(er.shader);
    set_mat4(er.shader, "u_model", glm::mat4(1.0f));
    set_mat4(er.shader, "u_view",  view);
    set_mat4(er.shader, "u_proj",  proj);
    glBindVertexArray(er.terrain_mesh.vao);
    glDrawArrays(GL_LINES, 0, er.terrain_mesh.count);
    glBindVertexArray(0);

    if (!placement_valid) return;
    static const int SEGS = 48;
    er.line_verts.clear();
    er.line_verts.reserve(SEGS * 12);
    for (int i = 0; i < SEGS; i++){
        float a0 = (float)i       / SEGS * 2.0f * 3.14159265f;
        float a1 = (float)(i + 1) / SEGS * 2.0f * 3.14159265f;
        float x0 = brush_pos.x + std::cos(a0) * brush_radius;
        float z0 = brush_pos.z + std::sin(a0) * brush_radius;
        float x1 = brush_pos.x + std::cos(a1) * brush_radius;
        float z1 = brush_pos.z + std::sin(a1) * brush_radius;
        float y0 = heightfield_sample(hf, x0, z0) + 0.1f;
        float y1 = heightfield_sample(hf, x1, z1) + 0.1f;
        er.line_verts.insert(er.line_verts.end(), { x0,y0,z0, 1.0f,1.0f,0.0f });
        er.line_verts.insert(er.line_verts.end(), { x1,y1,z1, 1.0f,1.0f,0.0f });
    }
    flush_line_batch(er, er.shader, view, proj);
}

void editor_renderer_draw_roads(EditorRenderer& er, const std::vector<RoadSpline>& roads,
    const glm::mat4& view, const glm::mat4& proj){
    if (roads.empty()) return;

    glm::vec3 LIGHT_DIR = glm::normalize(er.sun_dir);

    // road type colors
    // used when no texture is present
    static const glm::vec3 ROAD_COLORS[ROAD_COUNT] = {
        {0.20f, 0.20f, 0.20f}, // asphalt
        {0.55f, 0.48f, 0.38f}, // gravel
        {0.45f, 0.32f, 0.18f}, // dirt
        {0.85f, 0.78f, 0.55f}, // sand
        {0.25f, 0.55f, 0.18f}, // grass
        {0.70f, 0.70f, 0.68f}, // cement
        {0.90f, 0.88f, 0.60f}, // road_lines
    };

    /*
    ROAD SPLINE TEX NAMES ADD _COLOR.JPEG FILES IN ASSETS/
    AND CHANGE IT BASED ON NAME DEFS HERE
    */

     static const char* ROAD_TEX_NAMES[ROAD_COUNT] = {
        "asphalt.jpg",
        "gravel.jpg",
        "dirt.jpg",
        "sand.jpg",
        "grass.jpg",
        "cement.jpg",
        "road_lines.jpg",
    };

    auto& RL = er.road_loc;
    glm::mat4 identity = glm::mat4(1.0f);
    glm::mat3 nm = glm::mat3(1.0f);
    shader_bind(er.road_shader);
    glUniformMatrix4fv(RL.model, 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix4fv(RL.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(RL.proj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniformMatrix3fv(RL.normal_mat, 1, GL_FALSE, glm::value_ptr(nm));
    glUniform3f(RL.light_dir, LIGHT_DIR.x, LIGHT_DIR.y, LIGHT_DIR.z);
    glUniformMatrix4fv(RL.light_space, 1, GL_FALSE, glm::value_ptr(er.light_space_mat));
    glUniform1f(RL.shadow_bias, Const::SHADOW_BIAS);
    glUniform1f(RL.ambient, er.ambient);
    glUniform1f(RL.diff_intensity, er.diff_intensity);
    glUniform3f(RL.light_color, er.light_color.r, er.light_color.g, er.light_color.b);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, er.shadow_depth_tex);
    glUniform1i(RL.shadow_map, 1);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(RL.tex, 0);

    glm::vec3 cam_pos_r = glm::vec3(glm::inverse(view)[3]);
    glUniform3f(RL.fog_color, er.fog_color.r,  er.fog_color.g,  er.fog_color.b);
    glUniform1f(RL.fog_near, my_settings.render_fog ? er.fog_near : Const::CAM_FAR);
    glUniform1f(RL.fog_far,  my_settings.render_fog ? er.fog_far : Const::CAM_FAR + 1.0f);
    glUniform3f(RL.fog_cam_pos, cam_pos_r.x, cam_pos_r.y, cam_pos_r.z);
    upload_point_lights(er.road_shader.id, er.last_lights, cam_pos_r, er.night_factor);

    for (const auto& road : roads){
        if (road.vao == 0 || road.index_count == 0) continue;

        int type_idx = glm::clamp((int)road.type, 0, (int)ROAD_COUNT - 1);

        std::string tex_path = std::string("../assets/") + ROAD_TEX_NAMES[type_idx];
        GLuint tex = load_texture(er, tex_path);

        if (tex){
            glBindTexture(GL_TEXTURE_2D, tex);
            glUniform1i(RL.use_texture, 1);
        }
        else {
            glBindTexture(GL_TEXTURE_2D, 0);
            glUniform1i(RL.use_texture, 0);
            glm::vec3 kd = ROAD_COLORS[type_idx];
            glUniform3f(RL.kd, kd.r, kd.g, kd.b);
        }

        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset((road.type == ROAD_LINES) ? -2.0f : -1.0f, -1.0f);
        glBindVertexArray(road.vao);
        glDrawElements(GL_TRIANGLES, road.index_count, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

void editor_renderer_draw_ocean(EditorRenderer& er, Ocean& ocean,
    const glm::mat4& view, const glm::mat4& proj, float dt,
    float terrain_x_min, float terrain_x_max, float terrain_z_min, float terrain_z_max){
    if (!ocean.enabled) return;
    if (ocean.mesh_dirty)
        ocean_build_mesh(ocean, terrain_x_min, terrain_x_max, terrain_z_min, terrain_z_max);

    ocean.time += dt;

    glm::vec3 cam_pos = glm::vec3(glm::inverse(view)[3]);
    glm::vec3 light_dir = glm::normalize(er.sun_dir);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    auto& OL = er.ocean_loc;
    shader_bind(er.ocean_shader);
    glUniformMatrix4fv(OL.view,  1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(OL.proj,  1, GL_FALSE, glm::value_ptr(proj));
    glUniform1f(OL.time, ocean.time);
    glUniform1f(OL.y_level, ocean.y_level);
    glUniform3f(OL.light_dir, light_dir.x, light_dir.y, light_dir.z);
    glUniform3f(OL.cam_pos, cam_pos.x, cam_pos.y, cam_pos.z);
    glUniform3f(OL.light_color, er.light_color.r, er.light_color.g, er.light_color.b);
    glUniform1f(OL.ambient, er.ambient);
    glUniform1f(OL.diff_intensity, er.diff_intensity);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    if (ocean.mesh.vao && ocean.mesh.count > 0){
        glBindVertexArray(ocean.mesh.vao);
        glDrawElements(GL_TRIANGLES, ocean.mesh.count, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void editor_renderer_build_terrain_surface(EditorRenderer& er, const HeightField& hf, const Ocean& ocean){
    if (hf.rows < 2 || hf.cols < 2) return;

    // flat colors per surface type used when texture is missing
    static const glm::vec3 SURF_COLORS[(int)SURFACE_COUNT] = {
        {0.00f, 0.00f, 0.00f}, // none
        {0.20f, 0.20f, 0.20f}, // asphalt
        {0.55f, 0.48f, 0.38f}, // gravel
        {0.45f, 0.32f, 0.18f}, // dirt
        {0.85f, 0.78f, 0.55f}, // sand
        {0.25f, 0.55f, 0.18f}, // grass
        {0.70f, 0.70f, 0.68f}, // cement
        {0.55f, 0.52f, 0.48f}, // rock
    };

    // one sub-mesh per surface type so we can batch by texture
    // each bucket holds interleaved pos+normal+uv floats
    std::vector<float> buckets[(int)SURFACE_COUNT];

    auto get_pos = [&](int r, int c) -> glm::vec3 {
        return glm::vec3(
            hf.origin.x + c * hf.cell_size,
            hf.heights[r * hf.cols + c],
            hf.origin.z + r * hf.cell_size);
    };

    // returns the dominant surface type for a quad cell
    // uses top-left corner cell value — paint brush writes all covered cells
    auto cell_surface = [&](int r, int c) -> int {
        return (int)hf.surface[r * hf.cols + c];
    };
    for (int r = 0; r < hf.rows - 1; r++){
        for (int c = 0; c < hf.cols - 1; c++){
            // skip quads that fall inside any ocean zone
            float cx = hf.origin.x + (c + 0.5f) * hf.cell_size;
            float cz = hf.origin.z + (r + 0.5f) * hf.cell_size;
            bool in_ocean = ocean.enabled
                && hf.heights[r * hf.cols + c] < ocean.y_level
                && hf.heights[(r+1) * hf.cols + c] < ocean.y_level
                && hf.heights[r * hf.cols + (c+1)] < ocean.y_level
                && hf.heights[(r+1) * hf.cols + (c+1)] < ocean.y_level;
            if (in_ocean) continue;

            glm::vec3 p00 = get_pos(r, c);
            glm::vec3 p10 = get_pos(r+1, c);
            glm::vec3 p01 = get_pos(r,c+1);
            glm::vec3 p11 = get_pos(r+1, c+1);

            int si = cell_surface(r, c);
            auto& bucket = buckets[si];

            float u0 = (float)c, v0 = (float)r;
            float u1 = (float)(c+1), v1 = (float)(r+1);

            auto push_tri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 cc,
                                glm::vec2 uva, glm::vec2 uvb, glm::vec2 uvc){
                glm::vec3 n = glm::normalize(glm::cross(b - a, cc - a));
                auto push = [&](glm::vec3 p, glm::vec3 n_, glm::vec2 uv){
                    bucket.insert(bucket.end(), {
                        p.x, p.y, p.z, n_.x, n_.y, n_.z, uv.x, uv.y
                    });
                };
                push(a, n, uva);
                push(b, n, uvb);
                push(cc, n, uvc);
            };

            push_tri(p00, p10, p01, {u0,v0}, {u0,v1}, {u1,v0});
            push_tri(p10, p11, p01, {u0,v1}, {u1,v1}, {u1,v0});
        }
    }

    // rebuild per-surface VAOs stored in er.terrain_surface_mesh
    // we reuse the single Mesh slot for the first bucket and store extras inline
    // simplest approach: pack all buckets into one mesh, draw in surface-type passes
    // store bucket byte offsets so draw call can glDrawArrays with offset+count
    if (er.terrain_surface_mesh.vao){
        mesh_destroy(er.terrain_surface_mesh);
        er.terrain_surface_mesh.vao = 0;
        er.terrain_surface_mesh.vbo = 0;
        er.terrain_surface_mesh.count = 0;
    }

    // flatten all buckets into one buffer, record offsets
    std::vector<float> combined;
    er.terrain_surface_offsets.clear();
    er.terrain_surface_counts.clear();

    for (int i = 0; i < (int)SURFACE_COUNT; i++){
        int vert_count = (int)buckets[i].size() / 8; // 8 floats per vertex
        er.terrain_surface_offsets.push_back((int)combined.size() / 8);
        er.terrain_surface_counts.push_back(vert_count);
        combined.insert(combined.end(), buckets[i].begin(), buckets[i].end());
    }

    if (!combined.empty()){
        int vert_count = (int)combined.size() / 8;

        glGenVertexArrays(1, &er.terrain_surface_mesh.vao);
        glGenBuffers(1, &er.terrain_surface_mesh.vbo);
        glBindVertexArray(er.terrain_surface_mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, er.terrain_surface_mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,
            combined.size() * sizeof(float), combined.data(), GL_STATIC_DRAW);

        // pos
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        // normal
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);
        // uv
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(6*sizeof(float)));
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);
        er.terrain_surface_mesh.count = vert_count;
    }

    er.terrain_surface_dirty = false;
}

void editor_renderer_draw_terrain_surface(EditorRenderer& er, const HeightField& hf,
    const glm::mat4& view, const glm::mat4& proj, const Ocean& ocean){
    if (er.terrain_surface_dirty)
        editor_renderer_build_terrain_surface(er, hf, ocean);

    if (!er.terrain_surface_mesh.vao) return;
    glm::vec3 LIGHT_DIR = glm::normalize(er.sun_dir);
    static const glm::vec3 SURF_COLORS[(int)SURFACE_COUNT] = {
        {0.00f, 0.00f, 0.00f}, // none
        {0.20f, 0.20f, 0.20f}, // asphalt
        {0.55f, 0.48f, 0.38f}, // gravel
        {0.45f, 0.32f, 0.18f}, // dirt
        {0.85f, 0.78f, 0.55f}, // sand
        {0.25f, 0.55f, 0.18f}, // grass
        {0.70f, 0.70f, 0.68f}, // cement
        {0.55f, 0.52f, 0.48f}, // rock
    };

    /*
    SURFACE PAINT TEX NAMES ADD _COLOR.JPEG FILES IN ASSETS/
    AND CHANGE IT BASED ON NAME DEFS HERE
    */
    static const char* SURF_TEX_NAMES[(int)SURFACE_COUNT] = {
        "",
        "asphalt.jpg", 
        "gravel.jpg", 
        "dirt.jpg",
        "sand.jpg",    
        "grass.jpg",
        "cement.jpg",
        "rock.jpg",
    };

     auto& RL = er.road_loc;
    glm::mat4 identity = glm::mat4(1.0f);
    glm::mat3 nm = glm::mat3(1.0f);
    shader_bind(er.road_shader);
    glUniformMatrix4fv(RL.model, 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix4fv(RL.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(RL.proj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniformMatrix3fv(RL.normal_mat, 1, GL_FALSE, glm::value_ptr(nm));
    glUniform3f(RL.light_dir, LIGHT_DIR.x, LIGHT_DIR.y, LIGHT_DIR.z);
    glUniformMatrix4fv(RL.light_space, 1, GL_FALSE, glm::value_ptr(er.light_space_mat));
    glUniform1f(RL.shadow_bias, Const::SHADOW_BIAS);
    glUniform1f(RL.ambient, er.ambient);
    glUniform1f(RL.diff_intensity, er.diff_intensity);
    glUniform3f(RL.light_color, er.light_color.r, er.light_color.g, er.light_color.b);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, er.shadow_depth_tex);
    glUniform1i(RL.shadow_map, 1);
    glActiveTexture(GL_TEXTURE0);

    glm::vec3 cam_pos_t = glm::vec3(glm::inverse(view)[3]);
    glUniform3f(RL.fog_color, er.fog_color.r,  er.fog_color.g,  er.fog_color.b);
    glUniform1f(RL.fog_near, my_settings.render_fog ? er.fog_near : Const::CAM_FAR);
    glUniform1f(RL.fog_far,  my_settings.render_fog ? er.fog_far : Const::CAM_FAR + 1.0f);
    glUniform3f(RL.fog_cam_pos, cam_pos_t.x, cam_pos_t.y, cam_pos_t.z);
    upload_point_lights(er.road_shader.id, er.last_lights, cam_pos_t, er.night_factor);

    glBindVertexArray(er.terrain_surface_mesh.vao);

    for (int i = 0; i < (int)SURFACE_COUNT; i++){
        if (er.terrain_surface_counts[i] == 0) continue;
        if (i == (int)SURFACE_NONE) continue;

        std::string tex_path = std::string("../assets/") + SURF_TEX_NAMES[i];
        GLuint tex = load_texture(er, tex_path);
        if (tex){
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
            glUniform1i(RL.tex, 0);
            glUniform1i(RL.use_texture, 1);
        }
        else {
            glUniform1i(RL.use_texture, 0);
            glm::vec3 kd = SURF_COLORS[i];
            glUniform3f(RL.kd, kd.r, kd.g, kd.b);
        }

        glDrawArrays(GL_TRIANGLES,
            er.terrain_surface_offsets[i],
            er.terrain_surface_counts[i]);

        if (tex) glBindTexture(GL_TEXTURE_2D, 0);
    }

    glBindVertexArray(0);
}

void editor_renderer_draw_pose_mode(EditorRenderer& er, const EditorState& editor,
    const DriverModel& driver, const TrikeModel& trike,
    const glm::mat4& view, const glm::mat4& proj,
    const DriverModel* npc_model, const WorldMap& map)
{
    auto& OL = er.obj_loc;
    glm::vec3 ld = glm::normalize(er.sun_dir);
    shader_bind(er.obj_shader);
    glUniformMatrix4fv(OL.view,  1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(OL.proj,  1, GL_FALSE, glm::value_ptr(proj));
    glUniform3f(OL.light_dir, ld.x, ld.y, ld.z);
    glUniform3f(OL.light_color, er.light_color.r, er.light_color.g, er.light_color.b);
    glUniform1f(OL.ambient, er.ambient);
    glUniform1f(OL.diff_intensity, er.diff_intensity);
    glUniformMatrix4fv(OL.light_space, 1, GL_FALSE, glm::value_ptr(er.light_space_mat));
    glUniform1f(OL.shadow_bias, Const::SHADOW_BIAS);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, er.shadow_depth_tex);
    glUniform1i(OL.shadow_map, 1);
    glUniform1i(OL.use_texture, 0);
    glUniform1i(er.pt_light_loc.count, 0);
    glActiveTexture(GL_TEXTURE0);

    TrikeState dummy_trike;
    memset(&dummy_trike, 0, sizeof(dummy_trike));

    // trike always renders as reference 
    // same origin regardless of npc or driver
    trike_model_draw(trike, dummy_trike, er.obj_shader, view, proj);

    if (editor.pose_npc_id != -1 && npc_model){
        // build a scratch NpcState that holds the current editor pose
        // position at world origin so it renders relative to the trike reference
        NpcState scratch;
        scratch.position = glm::vec3(0.0f);
        scratch.yaw = 0.0f;

        // find the WorldObject for this NPC to grab its editor scale
        // without this, pose mode always renders at scale 1
        scratch.editor_scale = glm::vec3(1.0f);
        for (const auto& o : map.objects){
            if (o.id == editor.pose_npc_id){
                scratch.editor_scale = o.scale;
                break;
            }
        }
         scratch.mode = editor.pose_editing_hail ? NPC_HAILING : NPC_PASSENGER;
        // hail pose: render at world position, not trike-relative origin
        if (editor.pose_editing_hail)
            scratch.position = editor.pose_seat;

        // load whichever pose is being edited into the scratch state
        // Ctrl+H edits hail, Ctrl+M edits mount we mirror that here
        for (int i = 0; i < 6; i++){
            scratch.hail_pose_quat[i]    = editor.pose_quat[i];
            scratch.hail_pose_offset[i]  = editor.pose_offset[i];
            scratch.mount_pose_quat[i]   = editor.pose_quat[i];
            scratch.mount_pose_offset[i] = editor.pose_offset[i];
        }
        scratch.hail_pose_seat  = editor.pose_seat;
        scratch.mount_pose_seat = editor.pose_seat;
        

        npc_draw(scratch, *npc_model, er.obj_shader, view, proj);
    }
    else {
        // driver pose
        driver_model_draw_pose(
            driver,
            editor.pose_seat,
            editor.pose_quat,
            editor.pose_offset,
            editor.pose_bone,
            er.obj_shader,
            view,
            proj);
    }
}

void editor_renderer_draw_hud(EditorRenderer& er, const EditorState& editor, const WorldMap& map){
    if (editor.mode == MODE_TERRAIN){
        if (!editor.paint_mode){
            font_draw(er.font, "[ TERRAIN MODE ]  P=paint mode", 220, 16, 3, 0.30f, 0.90f, 0.25f);
            font_draw(er.font, "LMB=raise  RMB=lower  SHIFT=smooth  [/]=brush size  H=exit",
                220, 40, 2, 0.30f, 0.90f, 0.25f);
            char brush_buf[64];
            snprintf(brush_buf, sizeof(brush_buf), "BRUSH RADIUS: %.1fm", editor.brush_radius);
            font_draw(er.font, brush_buf, 220, 60, 2, 0.30f, 0.90f, 0.25f);
        }
        else {
            font_draw(er.font, "[ PAINT MODE ]  P=sculpt mode", 220, 16, 3, 0.95f, 0.70f, 0.10f);
            font_draw(er.font, "LMB=paint  S=spline  [/]=brush  0=erase 1=asphalt 2=gravel 3=dirt 4=sand 5=grass 6=cement 7=rock",
                220, 40, 2, 0.95f, 0.70f, 0.10f);
            font_draw(er.font, "Ctrl+Shift+W=wipe canvas  H=exit terrain",
                220, 58, 2, 0.95f, 0.70f, 0.10f);
            char surf_buf[64];
            snprintf(surf_buf, sizeof(surf_buf), "SURFACE: %s   BRUSH: %.1fm",
                Const::SURFACE_NAMES[(int)editor.paint_surface], editor.brush_radius);
            font_draw(er.font, surf_buf, 220, 76, 2, 0.95f, 0.70f, 0.10f);
        }
    }

    if (editor.mode == MODE_ROAD){
        static const char* ROAD_TYPE_NAMES[ROAD_COUNT] = {
            "ASPHALT", "GRAVEL", "DIRT", "SAND", "GRASS", "CEMENT", "ROAD_LINES"
        };
        font_draw(er.font, "[ ROAD MODE ]", 180, 16, 3, 0.25f, 0.75f, 1.00f);
        font_draw(er.font, "LMB=add point  RMB=undo  ENTER=finish  DEL=delete  [/]=road type  M=exit",
            220, 40, 2, 0.25f, 0.75f, 1.00f);
        bool found = false;
        for (const auto& r : map.roads){
            if (r.id == editor.active_road_id){
                char buf[64];
                const char* type_name = ROAD_TYPE_NAMES[glm::clamp((int)r.type, 0, (int)ROAD_COUNT - 1)];
                snprintf(buf, sizeof(buf), "TYPE: %s   POINTS: %d", type_name, (int)r.points.size());
                font_draw(er.font, buf, 220, 60, 2, 0.25f, 0.75f, 1.00f);
                found = true;
                break;
            }
        }
        if (!found){
            char buf[64];
            snprintf(buf, sizeof(buf), "TYPE: %s   (no active spline)",
                ROAD_TYPE_NAMES[glm::clamp((int)editor.active_road_id, 0, (int)ROAD_COUNT - 1)]);
            font_draw(er.font, buf, 220, 60, 2, 0.50f, 0.50f, 0.50f);
        }
    }

    if (editor.mode == MODE_OCEAN){
        font_draw(er.font, "[ OCEAN MODE ]", 180, 16, 3, 0.10f, 0.55f, 0.90f);
        font_draw(er.font, "PgUp/Dn=y level  E=toggle on/off  [/]=rebuild  O=exit",
            220, 40, 2, 0.10f, 0.55f, 0.90f);
        char buf[64];
        snprintf(buf, sizeof(buf), "OCEAN Y: %.2f  [PgUp/Dn] nudge  [E] toggle %s",
            map.ocean.y_level, map.ocean.enabled ? "ON" : "OFF");
        font_draw(er.font, buf, 220, 60, 2, 1.0f, 0.80f, 0.10f);
    }

    if (editor.mode == MODE_LIGHT){
        font_draw(er.font, "[ LIGHT MODE ]", 180, 16, 3, 1.0f, 0.90f, 0.30f);
        font_draw(er.font, "LMB=place/select  DEL=delete  Arrows=move XZ  PgUp/Dn=move Y  [/]=radius  +/-=intensity",
            220, 40, 2, 1.0f, 0.90f, 0.30f);
        font_draw(er.font, "Q/E=red  Z/X=green  C/V=blue  L=exit",
            220, 58, 2, 1.0f, 0.90f, 0.30f);
        if (editor.selected_light_id != -1){
            for (const auto& l : map.lights){
                if (l.id != editor.selected_light_id) continue;
                char buf[128];
                snprintf(buf, sizeof(buf), "POS (%.1f, %.1f, %.1f)  R:%.2f G:%.2f B:%.2f",
                    l.position.x, l.position.y, l.position.z,
                    l.color.r, l.color.g, l.color.b);
                font_draw(er.font, buf, 220, 76, 2, 1.0f, 1.0f, 1.0f);
                snprintf(buf, sizeof(buf), "RADIUS: %.1fm   INTENSITY: %.2f",
                    l.radius, l.intensity);
                font_draw(er.font, buf, 220, 94, 2, 1.0f, 1.0f, 1.0f);
                break;
            }
        }
    }

    if (editor.mode == MODE_AUDIO){
        static const char* SLOT_NAMES[] = {
            "impact", "proximity",
            "hail", "pickup", "yap",
            "dropoff_good", "dropoff_bad",
            "crash_mild", "crash_heavy", "crash_rollover"
        };
        static constexpr int AUDIO_SLOT_COUNT = 10;
        static constexpr int AUDIO_PAGE_SIZE  = 8;

        font_draw(er.font, "[ AUDIO MODE ]", 180, 16, 3, 0.80f, 0.40f, 1.00f);
        font_draw(er.font, "TAB=cycle slot  1-8=assign file  UP/DN=scroll  DEL=clear  Z=exit",
            220, 40, 2, 0.80f, 0.40f, 1.00f);

        const WorldObject* target = nullptr;
        for (const auto& o : map.objects)
            if (o.id == editor.selected_id){ target = &o; break; }

        if (target){
            int x = 16, y = 60;
            font_draw(er.font, "SLOTS", x, y, 2, 0.9f, 0.9f, 0.9f);
            y += 28;

            auto get_slot_val = [&](const WorldObject& o, int slot) -> const std::string& {
                switch(slot){
                    case 0: return o.audio_impact;
                    case 1: return o.audio_proximity;
                    case 2: return o.audio_hail;
                    case 3: return o.audio_pickup;
                    case 4: return o.audio_yap;
                    case 5: return o.audio_dropoff_good;
                    case 6: return o.audio_dropoff_bad;
                    case 7: return o.audio_crash_mild;
                    case 8: return o.audio_crash_heavy;
                    case 9: return o.audio_crash_rollover;
                    default: return o.audio_impact;
                }
            };

            for (int i = 0; i < AUDIO_SLOT_COUNT; i++){
                bool active = (i == editor.audio_slot);
                const std::string& val = get_slot_val(*target, i);
                std::string label = std::string(active ? "> " : "  ")
                    + SLOT_NAMES[i] + ": "
                    + (val.empty() ? "(none)" : val.substr(val.find_last_of('/') + 1));
                if (label.size() > 36) label = label.substr(0, 34) + "..";
                float r = active ? 0.0f : 0.55f;
                float g = active ? 1.0f : 0.55f;
                float b = active ? 0.8f : 0.55f;
                font_draw(er.font, label, x, y, active ? 2 : 1, r, g, b);
                y += active ? 22 : 18;
            }

            int x2 = 340, y2 = 60;
            font_draw(er.font, "FILES", x2, y2, 2, 0.9f, 0.9f, 0.9f);
            y2 += 28;
            int total = (int)editor.audio_file_list.size();
            if (total == 0){
                font_draw(er.font, "no .wav/.ogg in assets/audio/", x2, y2, 1, 0.5f, 0.5f, 0.5f);
            }
            else {
                int end = std::min(editor.audio_file_page + AUDIO_PAGE_SIZE, total);
                for (int i = editor.audio_file_page; i < end; i++){
                    int slot_key = i - editor.audio_file_page + 1;
                    std::string name = editor.audio_file_list[i];
                    name = name.substr(name.find_last_of('/') + 1);
                    if (name.size() > 28) name = name.substr(0, 26) + "..";
                    std::string line = std::to_string(slot_key) + " " + name;
                    font_draw(er.font, line, x2, y2, 1, 0.75f, 0.75f, 0.75f);
                    y2 += 18;
                }
                char pg[32];
                snprintf(pg, sizeof(pg), "[%d-%d / %d]  UP/DN scroll",
                    editor.audio_file_page + 1,
                    std::min(editor.audio_file_page + AUDIO_PAGE_SIZE, total), total);
                font_draw(er.font, pg, x2, y2 + 4, 1, 0.4f, 0.4f, 0.4f);
            }
            if (editor.audio_slot == 1){
                char buf[64];
                snprintf(buf, sizeof(buf), "RADIUS: %.1fm  ([/] to adjust)", target->audio_radius);
                font_draw(er.font, buf, 16, Const::WINDOW_HEIGHT - 100, 2, 0.80f, 0.40f, 1.00f);
            }
        }
        else {
            font_draw(er.font, "no object selected", 220, 60, 2, 0.5f, 0.5f, 0.5f);
        }
    }

    if (editor.mode == MODE_AMBIENCE){
        font_draw(er.font, "[ AMBIENCE MODE ]", 180, 16, 3, 0.30f, 0.95f, 0.60f);
        font_draw(er.font, "LMB=place/select  DEL=delete  F=type  [/]=radius  1-8=assign audio  UP/DN=scroll  I=exit",
            220, 40, 2, 0.30f, 0.95f, 0.60f);

        for (int z = 0; z < map.ambience_count; z++){
            const AmbienceZone& zone = map.ambience_zones[z];
            if (zone.id != editor.selected_zone_id) continue;

            static constexpr int AMB_PAGE_SIZE = 8;
            int x = 16, y = 60;
            font_draw(er.font, "FILES", x, y, 2, 0.9f, 0.9f, 0.9f);
            y += 28;
            int total = (int)editor.audio_file_list.size();
            if (total == 0){
                font_draw(er.font, "no .wav/.ogg in assets/audio/", x, y, 1, 0.5f, 0.5f, 0.5f);
            }
            else {
                int end = std::min(editor.ambience_file_page + AMB_PAGE_SIZE, total);
                for (int i = editor.ambience_file_page; i < end; i++){
                    int slot_key = i - editor.ambience_file_page + 1;
                    std::string name = editor.audio_file_list[i];
                    name = name.substr(name.find_last_of('/') + 1);
                    if (name.size() > 28) name = name.substr(0, 26) + "..";
                    font_draw(er.font, std::to_string(slot_key) + " " + name,
                        x, y, 1, 0.75f, 0.75f, 0.75f);
                    y += 18;
                }
                char pg[32];
                snprintf(pg, sizeof(pg), "[%d-%d / %d]",
                    editor.ambience_file_page + 1,
                    std::min(editor.ambience_file_page + AMB_PAGE_SIZE, total), total);
                font_draw(er.font, pg, x, y + 4, 1, 0.4f, 0.4f, 0.4f);
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "ZONE id=%d  TYPE: %s  RADIUS: %.1fm",
                zone.id,
                zone.type == AMBIENCE_NIGHT ? "NIGHT" : "PROXIMITY",
                zone.radius);
            font_draw(er.font, buf, 16, Const::WINDOW_HEIGHT - 120, 2, 0.30f, 0.95f, 0.60f);
            std::string apath = zone.audio_path[0] ? zone.audio_path : "(none)";
            std::string aname = apath.substr(apath.find_last_of('/') + 1);
            font_draw(er.font, "AUDIO: " + aname, 16, Const::WINDOW_HEIGHT - 98, 2, 0.30f, 0.95f, 0.60f);
            break;
        }
    }

    if (editor.mode == MODE_POSE){
        static const char* bone_names[6] = {
            "TORSO", "HEAD", "LEG_L", "LEG_R", "ARM_L", "ARM_R"
        };
        font_draw(er.font, "[ POSE MODE ]", 180, 16, 3, 1.0f, 0.60f, 0.10f);
        font_draw(er.font, "F=next bone  Arrows=rot XY  PgUp/Dn=rot Z  NP8/2=seat Z  NP4/6=seat X  NP+/-=seat Y",
            220, 40, 2, 1.0f, 0.60f, 0.10f);
        font_draw(er.font, "SHIFT=fine  ENTER=dump values  V=hail/mount toggle  K=exit", 220, 58, 2, 1.0f, 0.60f, 0.10f);
        if (editor.pose_npc_id != -1){
            const char* pose_label = editor.pose_editing_hail ? "EDITING: HAIL  [Ctrl+H to save]" : "EDITING: MOUNT  [Ctrl+M to save]";
            font_draw(er.font, pose_label, 220, 112, 2, editor.pose_editing_hail ? 0.4f : 0.2f, 1.0f, 0.4f);
        }
        const glm::quat& bq = editor.pose_quat[editor.pose_bone];
        float bangle = glm::degrees(2.0f * std::acos(glm::clamp(bq.w, -1.0f, 1.0f)));
        float bs = std::sqrt(std::max(0.0f, 1.0f - bq.w * bq.w));
        glm::vec3 baxis = (bs > 0.001f)
            ? glm::vec3(bq.x/bs, bq.y/bs, bq.z/bs)
            : glm::vec3(1,0,0);
        char buf[128];
        snprintf(buf, sizeof(buf), "BONE: %s  [%d]  axis(%.2f,%.2f,%.2f)  angle:%.1fdeg",
            bone_names[editor.pose_bone], editor.pose_bone,
            baxis.x, baxis.y, baxis.z, bangle);
        font_draw(er.font, buf, 220, 76, 2, 1.0f, 1.0f, 1.0f);
        if (editor.pose_numpad_translate){
            const glm::vec3& off = editor.pose_offset[editor.pose_bone];
            snprintf(buf, sizeof(buf), "NP=BONE TRANSLATE  X:%.3f  Y:%.3f  Z:%.3f  [NP0 for seat]",
                off.x, off.y, off.z);
            font_draw(er.font, buf, 220, 94, 2, 1.0f, 0.7f, 0.3f);
        }
        else {
            snprintf(buf, sizeof(buf), "NP=SEAT  X:%.3f  Y:%.3f  Z:%.3f  [NP0 for bone translate]",
                editor.pose_seat.x, editor.pose_seat.y, editor.pose_seat.z);
            font_draw(er.font, buf, 220, 94, 2, 0.7f, 1.0f, 0.7f);
        }
    }

    // prop palette
    if (editor.mode != MODE_AUDIO && editor.mode != MODE_AMBIENCE){
        const int PAGE_SIZE = Const::EDITOR_PAGE_SIZE;
        int total = (int)editor.prop_list.size();
        int x = 16, y = 60;
        font_draw(er.font, "PROPS", x, y, 2, 0.9f, 0.9f, 0.9f);
        y += 30;
        if (total == 0){
            font_draw(er.font, "no .obj in assets/", x, y, 1, 0.5f, 0.5f, 0.5f);
        }
        else {
            int page_start = editor.prop_page * PAGE_SIZE;
            int page_end   = std::min(page_start + PAGE_SIZE, total);
            for (int i = page_start; i < page_end; i++){
                int slot = i - page_start + 1;
                bool active = (editor.prop_list[i] == editor.selected_model);
                std::string name = editor.prop_list[i];
                if (name.size() > 20) name = name.substr(0, 18) + "..";
                std::string line = std::to_string(slot) + " " + name;
                if (active)
                    font_draw(er.font, line, x, y, 1, 0.0f, 1.0f, 1.0f);
                else
                    font_draw(er.font, line, x, y, 1, 0.7f, 0.7f, 0.7f);
                y += 20;
            }
            int max_page = (total - 1) / PAGE_SIZE;
            std::string page_str = "[ pg " + std::to_string(editor.prop_page + 1)
                + "/" + std::to_string(max_page + 1) + " ]";
            font_draw(er.font, page_str, x, y + 4, 3, 0.5f, 0.5f, 0.5f);
        }
    }

    // status HUD
    {
        int x = 16;
        int bottom = Const::WINDOW_HEIGHT - 220;
        int y = bottom;
        const char* tool_str = editor.tool == TOOL_TRANSLATE ? "TRANSLATE" :
            editor.tool == TOOL_ROTATE ? "ROTATE" : "SCALE";
        font_draw(er.font, std::string("TOOL: ") + tool_str, x, y, 2, 1.0f, 1.0f, 0.2f);
        y += 20;
        std::string model_label = "MODEL: " + (editor.selected_model.empty() ? "(none)" : editor.selected_model);
        font_draw(er.font, model_label, x, y, 2, 1.0f, 1.0f, 1.0f);
        y += 20;
        font_draw(er.font, "OBJECTS: " + std::to_string(map.objects.size()), x, y, 2, 0.7f, 0.7f, 0.7f);
        y += 20;
        if (editor.selected_id != -1){
            for (const auto& o : map.objects){
                if (o.id != editor.selected_id) continue;
                char buf[128];
                snprintf(buf, sizeof(buf), "POS X:%.1f  Y:%.1f  Z:%.1f",
                    o.position.x, o.position.y, o.position.z);
                font_draw(er.font, buf, x, y, 2, 0.6f, 1.0f, 0.6f);
                y += 20;
                snprintf(buf, sizeof(buf), "ROT Y:%.1f deg", glm::degrees(o.rotation.y));
                font_draw(er.font, buf, x, y, 2, 0.6f, 1.0f, 0.6f);
                y += 20;
                snprintf(buf, sizeof(buf), "SCALE X:%.2f  Y:%.2f  Z:%.2f",
                    o.scale.x, o.scale.y, o.scale.z);
                font_draw(er.font, buf, x, y, 2, 0.6f, 1.0f, 0.6f);
                y += 20;
                const char* bname = "STATIC";
                float br = 0.55f, bg = 0.55f, bb = 0.55f;
                switch(o.behavior){
                    case STATIC:     bname="STATIC";     br=0.55f; bg=0.55f; bb=0.55f; break;
                    case DYNAMIC:    bname="DYNAMIC";    br=0.20f; bg=0.50f; bb=1.00f; break;
                    case DECORATION: bname="DECORATION"; br=0.95f; bg=0.80f; bb=0.10f; break;
                    case PEDESTRIAN: bname="PEDESTRIAN"; br=0.20f; bg=0.85f; bb=0.30f; break;
                }
                snprintf(buf, sizeof(buf), "BEHAVIOR: %s  [B] cycle", bname);
                font_draw(er.font, buf, x, y, 2, br, bg, bb);
                y += 20;
                if (o.behavior == DYNAMIC){
                    snprintf(buf, sizeof(buf), "MASS:%.1f  REST:%.2f  FRIC:%.2f  [N] preset",
                        o.mass, o.restitution, o.friction);
                    font_draw(er.font, buf, x, y, 2, 0.4f, 0.8f, 1.0f);
                }
                break;
            }
        }
    }
}

void editor_renderer_draw_settings_menu(EditorRenderer& er, const EditorState& editor){
    draw_settings_overlay(er);
    // full-screen dark overlay so world is still visible but dimmed
    // drawn as a screen-space font overlay — no GL geometry needed
    // font coords are pixel space: 0,0 = top-left

    static const int SW = Const::WINDOW_WIDTH;
    static const int SH = Const::WINDOW_HEIGHT;
    static const int CX = SW / 2;

    // CONTROLS PAGE
    if (editor.settings_page == SETTINGS_PAGE_CONTROLS){

        struct ControlPage {
            const char* title;
            const char* desc;
            const char* keys[20][2]; // [key, action]
            int count;
        };

        static const ControlPage PAGES[] = {
            {
                "DRIVE MODE",
                "Controls while driving or on foot in the barangay.",
                {
                    { "W",            "Accelerate"                    },
                    { "S",            "Brake  /  Reverse"             },
                    { "A / D",        "Steer left / right"            },
                    { "E",            "Mount or dismount trike"       },
                    { "Q",            "Accept hailing passenger"      },
                    { "L",            "Toggle headlights"             },
                    { "P",            "Radio on / off"                },
                    { "/",            "Next radio track"              },
                    { "R",            "Reset trike and position"      },
                    { "F",            "Free camera toggle"            },
                    { "H",            "Show collision hitboxes"       },
                    { "Arrows",       "Orbit camera around trike"     },
                    { "TAB",          "Enter editor mode"             },
                    { "ESC",          "Open settings menu"            },
                },
                14
            },
            {
                "OBJECT MODE",
                "Place, select, and transform props in the world.",
                {
                    { "L Click",      "Place prop / select object"    },
                    { "Ctrl+Click",   "Select smallest object hit"    },
                    { "Shift+Click",  "Place on top of selected"      },
                    { "DEL",          "Delete selected object"        },
                    { "T",            "Translate tool"                },
                    { "R",            "Rotate tool"                   },
                    { "Y",            "Scale tool"                    },
                    { "Arrows",       "Move / rotate / scale"         },
                    { "Shift+Arrows", "Fine step (5cm)"               },
                    { "PgUp / PgDn",  "Nudge Y up / down"            },
                    { "B",            "Cycle behavior (Static etc)"   },
                    { "N",            "Cycle physics preset (Dynamic)"},
                    { "1-9",          "Select prop from palette"      },
                    { "[ / ]",        "Prev / next prop page"         },
                    { "Ctrl+C / V",   "Copy / paste object"           },
                    { "Ctrl+S",       "Save map"                      },
                    { "F5",           "Rescan assets folder"          },
                },
                17
            },
            {
                "TERRAIN  &  ROAD",
                "Sculpt the heightfield and lay road splines.",
                {
                    { "H",            "Toggle terrain sculpt mode"    },
                    { "L Click hold", "Raise terrain"                 },
                    { "R Click hold", "Lower terrain"                 },
                    { "Shift+Click",  "Smooth brush"                  },
                    { "[ / ]",        "Shrink / grow brush radius"    },
                    { "Ctrl+Z",       "Undo last sculpt stroke"       },
                    { "P",            "Toggle surface paint mode"     },
                    { "0-7",          "Select surface type to paint"  },
                    { "Ctrl+Shift+W", "Wipe entire surface canvas"    },
                    { "M",            "Toggle road spline mode"       },
                    { "L Click",      "Add spline control point"      },
                    { "R Click",      "Undo last control point"       },
                    { "[ / ]",        "Cycle road type"               },
                    { "ENTER",        "Finish spline"                 },
                    { "DEL",          "Delete active spline"          },
                    { "Ctrl+S",       "Save"                          },
                },
                16
            },
            {
                "LIGHT, OCEAN, AMBIENCE",
                "Place point lights, water zones, and ambient audio.",
                {
                    { "L",            "Toggle light placement mode"   },
                    { "L Click",      "Place or select a light"       },
                    { "Arrows",       "Move selected light XZ"        },
                    { "PgUp / PgDn",  "Move selected light Y"         },
                    { "[ / ]",        "Adjust light radius"           },
                    { "+ / -",        "Adjust intensity"              },
                    { "Q/E  Z/X  C/V","Tune R / G / B tint"           },
                    { "DEL",          "Delete selected light"         },
                    { "O",            "Toggle ocean mode"             },
                    { "PgUp / PgDn",  "Nudge ocean Y level"           },
                    { "E",            "Toggle ocean on / off"         },
                    { "I",            "Toggle ambience zone mode"     },
                    { "L Click",      "Place or select zone"          },
                    { "[ / ]",        "Adjust zone radius"            },
                    { "F",            "Toggle zone type (Night/Prox)" },
                    { "1-8",          "Assign audio file to zone"     },
                },
                16
            },
            {
                "POSE  &  AUDIO",
                "Edit driver and NPC bone poses. Assign object audio.",
                {
                    { "K",            "Toggle pose editor mode"       },
                    { "F",            "Cycle active bone"             },
                    { "Arrows",       "Rotate bone X / Y axis"        },
                    { "PgUp / PgDn",  "Rotate bone Z axis"            },
                    { "Shift+any",    "Fine rotation mode"            },
                    { "NP0",          "Toggle seat / bone translate"  },
                    { "NP8/2/4/6",    "Move seat or bone offset"      },
                    { "NP+ / NP-",    "Seat / bone Y up / down"       },
                    { "V",            "Toggle hail / mount pose"      },
                    { "Ctrl+H",       "Save hail pose to NPC"         },
                    { "Ctrl+M",       "Save mount pose to NPC"        },
                    { "Ctrl+S",       "Save driver pose to file"      },
                    { "ENTER",        "Dump pose as code to console"  },
                    { "Z",            "Toggle audio editor mode"      },
                    { "TAB",          "Cycle audio slot"              },
                    { "1-8",          "Assign audio file to slot"     },
                    { "DEL",          "Clear audio slot"              },
                },
                17
            },
        };
        static const int PAGE_COUNT = 5;

        // settings_cursor doubles as the sub-page index on the controls page
        // clamped in input handling
        int sub = glm::clamp(editor.settings_cursor, 0, PAGE_COUNT - 1);
        const ControlPage& cp = PAGES[sub];

        // header
        char pg_buf[32];
        snprintf(pg_buf, sizeof(pg_buf), "PAGE %d / %d", sub + 1, PAGE_COUNT);
        font_draw(er.font, "CONTROLS", 60, 140, 5, 1.0f, 1.0f, 1.0f);
        font_draw(er.font, pg_buf, 380, 152, 3, 0.5f, 0.5f, 0.5f);
        font_draw(er.font, cp.title, 60, 220, 4, 0.20f, 1.00f, 0.55f);
        font_draw(er.font, cp.desc, 60, 265, 2, 0.60f, 0.60f, 0.60f);

        // key list 
        // two columns
        int col_x[2] = { 60, 780 };
        int y = 295;
        int half = (cp.count + 1) / 2;
        for (int i = 0; i < cp.count; i++){
            int col = i / half;
            int row = i % half;
            int rx = col_x[col];
            int ry = y + row * 28;
            font_draw(er.font, cp.keys[i][0], rx, ry, 2, 0.90f, 0.85f, 0.40f);
            font_draw(er.font, cp.keys[i][1], rx + 200, ry, 2, 0.80f, 0.80f, 0.80f);
        }
        
        // nav hints
        font_draw(er.font, "LEFT / RIGHT = change page",
            60, SH - 58, 3, 0.6f, 0.6f, 0.6f);
        font_draw(er.font, "ESC = close ENTER = back to settings",
            60, SH - 28, 3, 0.6f, 0.6f, 0.6f);
        return;
    }



    // GRAPHICS PAGE
    if (editor.settings_page == SETTINGS_PAGE_GRAPHICS){
        font_draw(er.font, "GRAPHICS", 60, 80, 5, 1.0f, 1.0f, 1.0f);

        // preset row
        {
            bool sel = (editor.settings_cursor == 0);
            font_draw(er.font, "PRESET", 60, 170, 3, 0.9f, 0.9f, 0.9f);
            const char* presets[] = { "LOW", "MODERATE", "HIGH", "CUSTOM" };
            int px = 280;
            for (int i = 0; i < 4; i++){
                bool active = ((int)my_settings.preset == i);
                float r = active ? 0.20f : 0.45f;
                float g = active ? 1.00f : 0.45f;
                float b = active ? 0.55f : 0.45f;
                if (sel && active){ r = 1.0f; g = 1.0f; b = 0.3f; }
                font_draw(er.font, presets[i], px, 170, 3, r, g, b);
                px += (int)(strlen(presets[i]) * 20 + 50);
            }
        }

        // individual settings rows
        // layout: label | [-] value [+]
        struct Row {
            const char* label;
            const char* unit;
            float val;
            bool is_bool;
            bool bool_val;
        };

        Row rows[] = {
            { "SHADOW MAP SIZE", "px",  (float)my_settings.shadow_map_size,       false, false },
            { "SHADOW THROTTLE", "frm", (float)my_settings.shadow_throttle_frame, false, false },
            { "PROP CULL DIST",  "m",   my_settings.prop_cull_dist,               false, false },
            { "NPC CULL DIST",   "m",   my_settings.npc_cull_dist,                false, false },
            { "LIGHT CULL DIST", "m",   my_settings.light_cull_dist,              false, false },
            { "RAIN PARTICLES",  "",    (float)my_settings.rain_particle_count,   false, false },
            { "RAIN SPLASHES",   "",    (float)my_settings.rain_splash_max,       false, false },
            { "RENDER SHADOWS",  "",    0.0f, true,  my_settings.render_shadows },
            { "SHOW HUD",        "",    0.0f, true,  my_settings.show_hud       },
            { "RENDER FOG",      "",    0.0f, true,  my_settings.render_fog    },
        };
        static const int ROW_COUNT = 10;

        int y = 240;
        for (int i = 0; i < ROW_COUNT; i++){
            bool sel = (editor.settings_cursor == i + 1); // +1 because row 0 = preset
            float lr = sel ? 1.0f : 0.70f;
            float lg = sel ? 1.0f : 0.70f;
            float lb = sel ? 0.3f : 0.70f;

            font_draw(er.font, rows[i].label, 60, y, 3, lr, lg, lb);

            if (rows[i].is_bool){
                const char* bval = rows[i].bool_val ? "ON" : "OFF";
                float vr = rows[i].bool_val ? 0.20f : 0.70f;
                float vg = rows[i].bool_val ? 1.00f : 0.30f;
                float vb = rows[i].bool_val ? 0.55f : 0.30f;
                if (sel){ font_draw(er.font, "<", 540, y, 3, 1.0f, 1.0f, 1.0f); }
                font_draw(er.font, bval, 580, y, 3, vr, vg, vb);
                if (sel){ font_draw(er.font, ">", 660, y, 3, 1.0f, 1.0f, 1.0f); }
            }
            else {
                char vbuf[32];
                snprintf(vbuf, sizeof(vbuf), "%.0f %s", rows[i].val, rows[i].unit);
                if (sel){ font_draw(er.font, "<", 540, y, 3, 1.0f, 1.0f, 1.0f); }
                font_draw(er.font, vbuf, 580, y, 3, 0.85f, 0.85f, 0.85f);
                if (sel){ font_draw(er.font, ">", 760, y, 3, 1.0f, 1.0f, 1.0f); }
            }
            y += 42;
        }

        // back row
        {
            bool sel = (editor.settings_cursor == ROW_COUNT + 1);
            font_draw(er.font, "BACK", 60, y + 10, sel ? 4 : 3,
                sel ? 0.20f : 0.60f,
                sel ? 1.00f : 0.60f,
                sel ? 0.55f : 0.60f);
        }

        font_draw(er.font, "UP/DN=navigate  LEFT/RIGHT=adjust  ENTER=preset/back  ESC=close",
            60, SH - 50, 3, 0.6f, 0.6f, 0.6f);
        return;
    }

    // MAPS PAGE
    if (editor.settings_page == SETTINGS_PAGE_MAPS){
        font_draw(er.font, "MAPS", 60, 80, 5, 1.0f, 1.0f, 1.0f);

        int total = (int)g_maps.maps.size();
        int y = 180;

        if (g_maps.rename_mode){
            font_draw(er.font, "RENAME:", 60, y, 3, 0.90f, 0.90f, 0.30f);
            std::string display = g_maps.rename_buf + "_";
            font_draw(er.font, display, 260, y, 3, 1.0f, 1.0f, 1.0f);
            font_draw(er.font, "ENTER=confirm  BACKSPACE=delete",
                60, SH - 50, 3, 0.6f, 0.6f, 0.6f);
            return;
        }

        for (int i = 0; i < total; i++){
            bool sel  = (editor.settings_cursor == i);
            bool active = (i == g_maps.active_index);
            float r = sel ? 0.20f : (active ? 0.90f : 0.65f);
            float g2 = sel ? 1.00f : (active ? 0.90f : 0.65f);
            float b  = sel ? 0.55f : (active ? 0.30f : 0.65f);
            std::string label = (active ? "* " : "  ") + g_maps.maps[i].name;
            font_draw(er.font, label, 60, y, sel ? 4 : 3, r, g2, b);
            y += sel ? 52 : 40;
        }

        // [NEW MAP] row
        {
            bool sel = (editor.settings_cursor == total);
            font_draw(er.font, "+ NEW MAP", 60, y + 8, sel ? 4 : 3,
                sel ? 0.20f : 0.50f,
                sel ? 1.00f : 0.50f,
                sel ? 0.55f : 0.50f);
        }

        font_draw(er.font, "UP/DN=select  ENTER=switch  F2=rename  LEFT=back",
            60, SH - 50, 3, 0.6f, 0.6f, 0.6f);
        return;
    }

    // MAIN PAGE
    font_draw(er.font, "SETTINGS", 100, SH/2 - 100, 5, 1.0f, 1.0f, 1.0f);
    const char* items[] = { "GRAPHICS", "CONTROLS", "CHANGE MAPS", "QUIT" };
    for (int i = 0; i < 4; i++){
        bool sel = (editor.settings_cursor == i);
        float r = sel ? 0.20f : 0.60f;
        float g = sel ? 1.00f : 0.60f;
        float b = sel ? 0.55f : 0.60f;
        font_draw(er.font, items[i], 100, SH/2 - 10 + i * 50, 4, r, g, b);
    }

    font_draw(er.font, "UP/DN=navigate ENTER=select ESC=close",
        100, SH/2 + 230, 2, 0.4f, 0.4f, 0.4f);
}

void editor_renderer_destroy(EditorRenderer& er){
    shader_destroy(er.shader);
    shader_destroy(er.obj_shader);
    shader_destroy(er.road_shader);
    shader_destroy(er.ocean_shader);
    shader_destroy(er.depth_shader);
    mesh_destroy(er.grid);
    if (er.terrain_mesh.vao) mesh_destroy(er.terrain_mesh);
    if (er.terrain_surface_mesh.vao) mesh_destroy(er.terrain_surface_mesh);
    font_destroy(er.font);
    for (auto& [name, mesh] : er.prop_cache)
        obj_mesh_destroy(mesh);
    er.prop_cache.clear();
    for (auto& [path, id] : er.tex_cache)
        if (id) glDeleteTextures(1, &id);
    er.tex_cache.clear();
}