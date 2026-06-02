#include "editor_renderer.hpp"
#include "obj_loader.hpp"
#include "obj_mesh.hpp"
#include "../core/const.hpp"
#include "../world/world_object.hpp"
#include "../world/ocean.hpp"
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
#define STB_IMAGE_IMPLEMENTATION
#include "../../vendor/stb/stb_image.h"
#include <iostream>

// lit shader - pos + normal layout
// mirrors scene.cpp VERT_SRC/ FRAG_SRC
static const char* ER_LIT_VERT = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat3 u_normal_mat;
uniform mat4 u_light_space;
out vec3 v_world_normal;
out vec3 v_world_pos;
out vec2 v_uv;
out vec4 v_light_space_pos;
void main(){
    vec4 world = u_model * vec4(a_pos, 1.0);
    gl_Position = u_proj * u_view * world;
    v_world_normal = normalize(u_normal_mat * a_normal);
    v_world_pos = world.xyz;
    v_uv = a_uv;
    v_light_space_pos = u_light_space * world;
}
)";

static const char* ER_LIT_FRAG = R"(
#version 330 core
in vec3 v_world_normal;
in vec3 v_world_pos;
in vec2 v_uv;
in vec4 v_light_space_pos;
out vec4 frag_color;
uniform vec3      u_kd;
uniform vec3      u_light_dir;
uniform sampler2D u_tex;
uniform sampler2D u_shadow_map;
uniform int       u_use_texture;
uniform float     u_shadow_bias;
uniform float     u_ambient;
uniform float     u_diff_intensity;
uniform vec3      u_light_color;

#define MAX_LIGHTS 150
uniform int   u_light_count;
uniform vec3  u_light_pos[MAX_LIGHTS];
uniform vec3  u_light_color_pt[MAX_LIGHTS];
uniform float u_light_radius[MAX_LIGHTS];
uniform float u_light_intensity[MAX_LIGHTS];

float shadow_pcf(vec4 lsp, vec3 normal, vec3 ldir){
    // perspective divide -> NDC
    vec3 proj = lsp.xyz / lsp.w;
    // remap [-1,1] to [0,1] for texture lookup
    proj = proj * 0.5 + 0.5;
    // outside shadow frustum = fully lit
    if (proj.z > 1.0) return 0.0;
    float current_depth = proj.z;
    float bias = max(u_shadow_bias * (1.0 - dot(normal, ldir)), u_shadow_bias * 0.1);
    // PCF: 3x3 filter
    float shadow = 0.0;
    vec2 texel = 1.0 / textureSize(u_shadow_map, 0);
    for (int x = -1; x <= 1; x++){
        for (int y = -1; y <= 1; y++){
            float pcf_depth = texture(u_shadow_map, proj.xy + vec2(x,y) * texel).r;
            shadow += (current_depth - bias > pcf_depth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main(){
    vec3 n = normalize(v_world_normal);
    vec3 ldir = normalize(u_light_dir);
    float diff = max(dot(n, ldir), 0.0);
    float ambient = u_ambient;
    vec4 tex_sample = (u_use_texture == 1) ? texture(u_tex, v_uv) : vec4(u_kd, 1.0);
    if (u_use_texture == 1 && tex_sample.a < 0.5) discard;
    float shadow = shadow_pcf(v_light_space_pos, n, ldir);
    // shadow reduces diffuse but not ambient
    vec3 lit = tex_sample.rgb * u_light_color * (ambient + diff * u_diff_intensity * (1.0 - shadow));

    for (int i = 0; i < u_light_count; i++){
        float dist = length(u_light_pos[i] - v_world_pos);
        if (dist >= u_light_radius[i]) continue;
        float atten = 1.0 - (dist / u_light_radius[i]);
        atten *= atten;
        vec3 l = normalize(u_light_pos[i] - v_world_pos);
        float ndotl = max(dot(n, l), 0.0);
        lit += tex_sample.rgb * u_light_color_pt[i] * ndotl * atten * u_light_intensity[i];
    }

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

// loads a texture from disk into GL, caches by path
// returns 0 on failure
static GLuint load_texture(EditorRenderer& er, const std::string& path){
    if (path.empty()) return 0;
    auto it = er.tex_cache.find(path);
    if (it != er.tex_cache.end()) return it->second;

    stbi_set_flip_vertically_on_load(1); // OpenGL UVs are bottom-up
    int w, h, ch;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!px){
        std::cerr << "[tex] failed to load: " << path << "\n";
        er.tex_cache[path] = 0;
        return 0;
    }

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    // nearest filtering = PS1 look
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(px);
    er.tex_cache[path] = id;
    std::cout << "[tex] loaded " << w << "x" << h << " " << path << "\n";
    return id;
}

// draws a wireframe box from world space min/max cornders
// then uplaods a throwawau vao.vbo each call & destroys after drawing
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

static const char* ER_ROAD_VERT = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat3 u_normal_mat;
uniform mat4 u_light_space;
out vec3 v_world_normal;
out vec3 v_world_pos;
out vec2 v_uv;
out vec4 v_light_space_pos;
void main(){
    vec4 world = u_model * vec4(a_pos, 1.0);
    gl_Position = u_proj * u_view * world;
    v_world_normal = normalize(u_normal_mat * a_normal);
    v_world_pos = world.xyz;
    v_uv = a_uv;
    v_light_space_pos = u_light_space * world;
}
)";

static const char* ER_ROAD_FRAG = R"(
#version 330 core
in vec3 v_world_normal;
in vec3 v_world_pos;
in vec2 v_uv;
in vec4 v_light_space_pos;
out vec4 frag_color;
uniform vec3      u_kd;
uniform vec3      u_light_dir;
uniform sampler2D u_tex;
uniform sampler2D u_shadow_map;
uniform int       u_use_texture;
uniform float     u_shadow_bias;
uniform float     u_ambient;
uniform float     u_diff_intensity;
uniform vec3      u_light_color;

#define MAX_LIGHTS 150
uniform int   u_light_count;
uniform vec3  u_light_pos[MAX_LIGHTS];
uniform vec3  u_light_color_pt[MAX_LIGHTS];
uniform float u_light_radius[MAX_LIGHTS];
uniform float u_light_intensity[MAX_LIGHTS];

float shadow_pcf(vec4 lsp, vec3 normal, vec3 ldir){
    vec3 proj = lsp.xyz / lsp.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    float bias = max(u_shadow_bias * (1.0 - dot(normal, ldir)), u_shadow_bias * 0.1);
    float shadow = 0.0;
    vec2 texel = 1.0 / textureSize(u_shadow_map, 0);
    for (int x = -1; x <= 1; x++){
        for (int y = -1; y <= 1; y++){
            float pcf_depth = texture(u_shadow_map, proj.xy + vec2(x,y) * texel).r;
            shadow += (proj.z - bias > pcf_depth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main(){
    vec3 n = normalize(v_world_normal);
    vec3 ldir = normalize(u_light_dir);
    float diff = max(dot(n, ldir), 0.0);
    vec4 tex_col = (u_use_texture == 1) ? texture(u_tex, v_uv) : vec4(u_kd, 1.0);
    float shadow = shadow_pcf(v_light_space_pos, n, ldir);
    vec3 lit = tex_col.rgb * u_light_color * (u_ambient + diff * u_diff_intensity * (1.0 - shadow));

    for (int i = 0; i < u_light_count; i++){
        float dist = length(u_light_pos[i] - v_world_pos);
        if (dist >= u_light_radius[i]) continue;
        float atten = 1.0 - (dist / u_light_radius[i]);
        atten *= atten;
        vec3 l = normalize(u_light_pos[i] - v_world_pos);
        float ndotl = max(dot(n, l), 0.0);
        lit += tex_col.rgb * u_light_color_pt[i] * ndotl * atten * u_light_intensity[i];
    }

    frag_color = vec4(lit, 1.0);
}
)";



// ocean shader
// vertex: two sine waves animate Y, preserves vertex color depth hint
// fragment: blends deep/shallow color, adds specular shimmer
static const char* ER_OCEAN_VERT = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_color;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform float u_time;
uniform float u_amp;
uniform float u_freq;
uniform float u_speed;
uniform float u_amp2;
uniform float u_freq2;
uniform float u_speed2;
out vec3 v_color;
out vec3 v_world_pos;
void main(){
    vec3 p = a_pos;
    float w1 = sin(p.x * u_freq  + p.z * u_freq  * 0.62 + u_time * u_speed)  * u_amp;
    float w2 = sin(p.x * u_freq2 * 0.71 + p.z * u_freq2 + u_time * u_speed2) * u_amp2;
    p.y += w1 + w2;
    gl_Position = u_proj * u_view * u_model * vec4(p, 1.0);
    v_color = a_color;
    v_world_pos = p;
}
)";

static const char* ER_OCEAN_FRAG = R"(
#version 330 core
in vec3 v_color;
in vec3 v_world_pos;
out vec4 frag_color;
uniform vec3 u_light_dir;
void main(){
    // fake specular shimmer using world pos derivatives
    // gives moving highlight bands without real normals
    float shimmer = pow(max(0.0, sin(v_world_pos.x * 1.8 + v_world_pos.z * 1.2)), 6.0) * 0.18;
    vec3 col = v_color + shimmer;
    // slight darkening at edges from ambient
    float amb = 0.65;
    frag_color = vec4(col * amb, 0.88);
}
)";

static const char* ER_DEPTH_VERT = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_light_space;
uniform mat4 u_model;
void main(){
    gl_Position = u_light_space * u_model * vec4(a_pos, 1.0);
}
)";

static const char* ER_DEPTH_FRAG = R"(
#version 330 core
void main(){}
)";

void editor_renderer_init(EditorRenderer& er){
    shader_init(er.shader, ER_VERT, ER_FRAG);
    shader_init(er.obj_shader, ER_LIT_VERT, ER_LIT_FRAG);
    shader_init(er.road_shader, ER_ROAD_VERT, ER_ROAD_FRAG);
    shader_init(er.ocean_shader, ER_OCEAN_VERT, ER_OCEAN_FRAG);
    font_init(er.font, Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT);
    shader_init(er.depth_shader, ER_DEPTH_VERT, ER_DEPTH_FRAG);

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
    };
    cache_obj();
    cache_road();
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
        }
    }

    // init persistent line batch with empty buffer
    // sized for worst case: 200 wire boxes * 24 verts each
    er.line_verts.reserve(200 * 24 * 6);
    std::vector<float> empty(6, 0.0f);
    mesh_init(er.line_batch, empty);



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
    int count = 0;
    for (auto& [name, mesh] : er.prop_cache){
        for (int i = 0; i < (int)mesh.data.groups.size(); i++){
            const ObjGroup& grp = mesh.data.groups[i];
            const ObjMaterial* mat = obj_find_material(mesh.data, grp.mat_name);
            if (mat && !mat->tex_path.empty()){
                load_texture(er, mat->tex_path);
                count++;
            }
        }
    }
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

void editor_renderer_draw(EditorRenderer& er, const EditorState& editor, const WorldMap& map,
    const glm::mat4& view, const glm::mat4& proj, bool show_hitboxes,
    const std::vector<LightSource>& lights){

    // light dir which matches scene.cpp
    glm::vec3 LIGHT_DIR = glm::normalize(er.sun_dir);

    // bind shader and set shared uniforms once
    // model is identity for grid
    // each box override per draw_wire_box call
    shader_bind(er.shader);
    set_mat4(er.shader, "u_model", glm::mat4(1.0f));
    set_mat4(er.shader, "u_view", view);
    set_mat4(er.shader, "u_proj", proj);

    // 1. draw snap grid
    // static mesh
    if (editor.mode == MODE_OBJECT){
        glBindVertexArray(er.grid.vao);
        glDrawArrays(GL_LINES, 0, er.grid.count);
        glBindVertexArray(0);
    }

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

        // draw a wire circle on the ground at each light's XZ + a vertical stem
        er.line_verts.clear();
        for (const auto& l : map.lights){
            bool selected = (l.id == editor.selected_light_id);
            glm::vec3 col = selected ? glm::vec3(1.0f, 1.0f, 0.0f) : glm::vec3(l.color);

            // vertical stem from ground to light position
            float ground_y = heightfield_sample(map.terrain, l.position.x, l.position.z);
            er.line_verts.insert(er.line_verts.end(),
                {l.position.x, ground_y, l.position.z, col.r, col.g, col.b});
            er.line_verts.insert(er.line_verts.end(),
                {l.position.x, l.position.y, l.position.z, col.r, col.g, col.b});

            // radius circle on the ground
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

        // selected light info HUD
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


    if (editor.mode == MODE_POSE){
        static const char* bone_names[6] = {
            "TORSO", "HEAD", "LEG_L", "LEG_R", "ARM_L", "ARM_R"
        };
        font_draw(er.font, "[ POSE MODE ]", 180, 16, 3, 1.0f, 0.60f, 0.10f);
        font_draw(er.font, "F=next bone  Arrows=rot XY  PgUp/Dn=rot Z  NP8/2=seat Z  NP4/6=seat X  NP+/-=seat Y",
            220, 40, 2, 1.0f, 0.60f, 0.10f);
        font_draw(er.font, "SHIFT=fine  ENTER=dump values  K=exit", 220, 58, 2, 1.0f, 0.60f, 0.10f);

        // convert active bone quat to axis-angle for HUD display
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
        

        snprintf(buf, sizeof(buf), "SEAT  X:%.3f  Y:%.3f  Z:%.3f",
            editor.pose_seat.x, editor.pose_seat.y, editor.pose_seat.z);
        font_draw(er.font, buf, 220, 94, 2, 0.7f, 1.0f, 0.7f);
    }

    // 2. wireframe box colored by behavior
    // gives visual feedback for every placed object even before OBJ meshes load
    if (!show_hitboxes) goto skip_wireframes;
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
    skip_wireframes:

    // draw placed object meshes
    editor_renderer_draw_props(er, map, view, proj, {}, {}, lights);

    // 3. ghost box
    // only drawn when cursor is over valid ground and a model is selected
    if (editor.placement_valid && !editor.selected_model.empty()){
        glm::vec3 gp = editor.ghost_pos;
        er.line_verts.clear();
        push_wire_box(er.line_verts,
            gp + glm::vec3(-0.5f, 0.0f, -0.5f),
            gp + glm::vec3( 0.5f, 1.0f,  0.5f),
            {0.0f, 1.0f, 1.0f});
        flush_line_batch(er, er.shader, view, proj);
    }

    // 4. selection highlight
    // wraps curr selected placed obj
    // walks the object list to find the selected id
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
                y += 20;

                if (o.behavior == DYNAMIC){
                    // for now hud is ass because there no preset named stored for objs
                    // will add in the future. Testing values for now before I scale furthe
                    snprintf(buf, sizeof(buf), "MASS:%.1f  REST:%.2f  FRIC:%.2f  [N] preset",
                        o.mass, o.restitution, o.friction);
                    font_draw(er.font, buf, x, y, 2, 0.4f, 0.8f, 1.0f);
                }
                break;
            }
        }
    }

    // road mode cursor small diamond at ghost pos so you can see where clicks land
    if (editor.mode == MODE_ROAD && editor.placement_valid){
        glm::vec3 p = editor.ghost_pos;
        float s = 0.4f; // diamond half-size in metres

        // 6 points of a diamond: top, bottom, left, right, front, back
        glm::vec3 top = p + glm::vec3( 0,  s,  0);
        glm::vec3 bot = p + glm::vec3( 0, -s,  0);
        glm::vec3 lft = p + glm::vec3(-s,  0,  0);
        glm::vec3 rgt = p + glm::vec3( s,  0,  0);
        glm::vec3 fwd = p + glm::vec3( 0,  0, -s);
        glm::vec3 bck = p + glm::vec3( 0,  0,  s);

        // 12 edges connecting the 6 points
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

        // shadow cull
        glm::vec3 diff = o.position - er.shadow_cull_center;
        static constexpr float SHADOW_CULL_SQ = 180.0f * 180.0f;
        if (glm::dot(diff, diff) > SHADOW_CULL_SQ) continue;

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
    const std::vector<LightSource>& lights){

    glm::vec3 LIGHT_DIR = glm::normalize(er.sun_dir);

    auto& OL = er.obj_loc;
    shader_bind(er.obj_shader);
    glUniformMatrix4fv(OL.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(OL.proj, 1, GL_FALSE, glm::value_ptr(proj));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform3f(OL.light_dir, LIGHT_DIR.x, LIGHT_DIR.y, LIGHT_DIR.z);
    glUniformMatrix4fv(OL.light_space, 1, GL_FALSE, glm::value_ptr(er.light_space_mat));
    glUniform1f(OL.shadow_bias, Const::SHADOW_BIAS);
    glUniform1f(OL.ambient, er.ambient);
    glUniform1f(OL.diff_intensity, er.diff_intensity);
    glUniform3f(OL.light_color, er.light_color.r, er.light_color.g, er.light_color.b);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, er.shadow_depth_tex);
    glUniform1i(OL.shadow_map, 1);
    glActiveTexture(GL_TEXTURE0);

    // upload point lights
    
    // cpu cull

    // cpu cull: only upload lights within draw distance of camera
    glm::vec3 cam_pos = glm::vec3(glm::inverse(view)[3]);
    int active_lcount = 0;
    if (er.night_factor >= 0.01f){
        for (int i = 0; i < (int)lights.size() && active_lcount < Const::MAX_POINT_LIGHTS; i++){
            glm::vec3 d = lights[i].position - cam_pos;
            if (glm::dot(d, d) > Const::LIGHT_CULL_DIST_SQ) continue;
            // compact into slots 0..active_lcount
            glUniform3f(er.pt_light_loc.pos[active_lcount], lights[i].position.x, lights[i].position.y, lights[i].position.z);
            glUniform3f(er.pt_light_loc.color[active_lcount], lights[i].color.r, lights[i].color.g, lights[i].color.b);
            glUniform1f(er.pt_light_loc.radius[active_lcount], lights[i].radius);
            glUniform1f(er.pt_light_loc.intensity[active_lcount], lights[i].intensity * er.night_factor);
            active_lcount++;
        }
    }
    glUniform1i(er.pt_light_loc.count, active_lcount);
 
    // extract camera position from inverse view matrix
    // view is already passed in so no extra cost
    er.last_lights = lights;

    for (auto& o : map.objects){
        if (o.model_path.empty()) continue;

        // distance cull
        glm::vec3 diff = o.position - cam_pos;
        if (glm::dot(diff, diff) > Const::PROP_CULL_DIST_SQ) continue;

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

    if (er.terrain_mesh.vao) mesh_destroy(er.terrain_mesh);
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

    // upload point lights to road shader
    // road_loc doesn't have pt_light atm slots so look them up directly
    GLuint rid = er.road_shader.id;
    glm::vec3 cam_pos_r = glm::vec3(glm::inverse(view)[3]);
    int active_lcount = 0;
    char _buf[64];
    if (er.night_factor >= 0.01f){
        for (int i = 0; i < (int)er.last_lights.size() && active_lcount < Const::MAX_POINT_LIGHTS; i++){
            glm::vec3 d = er.last_lights[i].position - cam_pos_r;
            if (glm::dot(d, d) > Const::LIGHT_CULL_DIST_SQ) continue;
            snprintf(_buf, sizeof(_buf), "u_light_pos[%d]", active_lcount);
            glUniform3f(glGetUniformLocation(rid, _buf),
                er.last_lights[i].position.x, er.last_lights[i].position.y, er.last_lights[i].position.z);
            snprintf(_buf, sizeof(_buf), "u_light_color_pt[%d]", active_lcount);
            glUniform3f(glGetUniformLocation(rid, _buf),
                er.last_lights[i].color.r, er.last_lights[i].color.g, er.last_lights[i].color.b);
            snprintf(_buf, sizeof(_buf), "u_light_radius[%d]", active_lcount);
            glUniform1f(glGetUniformLocation(rid, _buf), er.last_lights[i].radius);
            snprintf(_buf, sizeof(_buf), "u_light_intensity[%d]", active_lcount);
            glUniform1f(glGetUniformLocation(rid, _buf), er.last_lights[i].intensity * er.night_factor);
            active_lcount++;
        }
    }
    glUniform1i(glGetUniformLocation(rid, "u_light_count"), active_lcount);

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

    er.ocean_time += dt;

    glm::vec3 LIGHT_DIR = glm::normalize(er.sun_dir);
    shader_bind(er.ocean_shader);
    set_mat4(er.ocean_shader, "u_model", glm::mat4(1.0f));
    set_mat4(er.ocean_shader, "u_view",  view);
    set_mat4(er.ocean_shader, "u_proj",  proj);

    glUniform1f(glGetUniformLocation(er.ocean_shader.id, "u_time"), er.ocean_time);
    glUniform1f(glGetUniformLocation(er.ocean_shader.id, "u_amp"), Const::OCEAN_WAVE_AMP);
    glUniform1f(glGetUniformLocation(er.ocean_shader.id, "u_freq"), Const::OCEAN_WAVE_FREQ);
    glUniform1f(glGetUniformLocation(er.ocean_shader.id, "u_speed"), Const::OCEAN_WAVE_SPEED);
    glUniform1f(glGetUniformLocation(er.ocean_shader.id, "u_amp2"), Const::OCEAN_WAVE_AMP2);
    glUniform1f(glGetUniformLocation(er.ocean_shader.id, "u_freq2"), Const::OCEAN_WAVE_FREQ2);
    glUniform1f(glGetUniformLocation(er.ocean_shader.id, "u_speed2"), Const::OCEAN_WAVE_SPEED2);
    glUniform3f(glGetUniformLocation(er.ocean_shader.id, "u_light_dir"),
        LIGHT_DIR.x, LIGHT_DIR.y, LIGHT_DIR.z);

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
    int skipped = 0;
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
            if (in_ocean){ skipped++; continue; }

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
    if (er.terrain_surface_mesh.vao) mesh_destroy(er.terrain_surface_mesh);

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

    // UPLOAD POINT LIGHTS TO ROAD SHADER
    GLuint rid = er.road_shader.id;
    glm::vec3 cam_pos_t = glm::vec3(glm::inverse(view)[3]);
    int active_lcount = 0;
    char _buf[64];
    if (er.night_factor >= 0.01f){
        for (int i = 0; i < (int)er.last_lights.size() && active_lcount < Const::MAX_POINT_LIGHTS; i++){
            glm::vec3 d = er.last_lights[i].position - cam_pos_t;
            if (glm::dot(d, d) > Const::LIGHT_CULL_DIST_SQ) continue;
            snprintf(_buf, sizeof(_buf), "u_light_pos[%d]", active_lcount);
            glUniform3f(glGetUniformLocation(rid, _buf),
                er.last_lights[i].position.x, er.last_lights[i].position.y, er.last_lights[i].position.z);
            snprintf(_buf, sizeof(_buf), "u_light_color_pt[%d]", active_lcount);
            glUniform3f(glGetUniformLocation(rid, _buf),
                er.last_lights[i].color.r, er.last_lights[i].color.g, er.last_lights[i].color.b);
            snprintf(_buf, sizeof(_buf), "u_light_radius[%d]", active_lcount);
            glUniform1f(glGetUniformLocation(rid, _buf), er.last_lights[i].radius);
            snprintf(_buf, sizeof(_buf), "u_light_intensity[%d]", active_lcount);
            glUniform1f(glGetUniformLocation(rid, _buf), er.last_lights[i].intensity * er.night_factor);
            active_lcount++;
        }
    }
    glUniform1i(glGetUniformLocation(rid, "u_light_count"), active_lcount);

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
    const glm::mat4& view, const glm::mat4& proj)
{
    // prime obj_shader with lighting state — same setup as draw_props
    // without this the shader has uninitialized uniforms and draws black/nothing
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
    glUniform1i(er.pt_light_loc.count, 0); // no point lights in pose mode
    glActiveTexture(GL_TEXTURE0);

    TrikeState dummy_trike;
    memset(&dummy_trike, 0, sizeof(dummy_trike));

    trike_model_draw(trike, dummy_trike, er.obj_shader, view, proj);

    driver_model_draw_pose(
        driver,
        editor.pose_seat,
        editor.pose_quat,
        editor.pose_bone,
        er.obj_shader,
        view,
        proj);
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