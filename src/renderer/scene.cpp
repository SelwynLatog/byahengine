#include "scene.hpp"
#include "mesh_builder.hpp"
#include "obj_loader.hpp"
#include "../core/const.hpp"
#include "../physics/trike_aabb.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <iostream>
#include <cmath>
#include "../../vendor/stb/stb_image.h"

// shader sources
static const char* VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat3 u_normal_mat;
uniform mat4 u_light_space;
out vec3 v_world_normal;
out vec3 v_world_pos;
out vec4 v_light_space_pos;
void main(){
    vec4 world = u_model * vec4(a_pos, 1.0);
    gl_Position = u_proj * u_view * world;
    v_world_normal = normalize(u_normal_mat * a_normal);
    v_world_pos = world.xyz;
    v_light_space_pos = u_light_space * world;
}
)";

static const char* FRAG_SRC = R"(
#version 330 core
in vec3 v_world_normal;
in vec3 v_world_pos;
in vec4 v_light_space_pos;
out vec4 frag_color;
uniform vec3      u_kd;
uniform vec3      u_kd_alt;
uniform vec3      u_light_dir;
uniform vec3      u_light_color;
uniform float     u_checker_scale;
uniform int       u_use_checker;
uniform float     u_ambient;
uniform float     u_diff_intensity;
uniform sampler2D u_shadow_map;
uniform float     u_shadow_bias;

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
    float shadow = shadow_pcf(v_light_space_pos, n, ldir);
    vec3 kd = u_kd;
    if (u_use_checker == 1){
        vec2 tile = floor(v_world_pos.xz * u_checker_scale);
        float parity = mod(tile.x + tile.y, 2.0);
        kd = mix(u_kd, u_kd_alt, parity);
    }
    vec3 lit = kd * u_light_color * (u_ambient + diff * u_diff_intensity * (1.0 - shadow));

    // point lights accumulated on top of sun pass
    for (int i = 0; i < u_light_count; i++){
        float dist = length(u_light_pos[i] - v_world_pos);
        if (dist >= u_light_radius[i]) continue;
        float atten = 1.0 - (dist / u_light_radius[i]);
        atten *= atten; // quadratic falloff
        vec3 l = normalize(u_light_pos[i] - v_world_pos);
        float ndotl = max(dot(n, l), 0.0);
        lit += kd * u_light_color_pt[i] * ndotl * atten * u_light_intensity[i];
    }

    frag_color = vec4(lit, 1.0);
}
)";

static const char* GIZMO_VERT_SRC = R"(
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

static const char* GIZMO_FRAG_SRC = R"(
#version 330 core
in  vec3 v_color;
out vec4 frag_color;
void main(){
    frag_color = vec4(v_color, 1.0);
}
)";

static const char* SKY_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
out vec2 v_ndc;
void main(){
    v_ndc = a_pos;
    gl_Position = vec4(a_pos, 0.9999, 1.0);
}
)";

static const char* SKY_FRAG_SRC = R"(
#version 330 core
in vec2 v_ndc;
out vec4 frag_color;
uniform mat4      u_inv_view_proj;
uniform sampler2D u_sky_tex;       // day tex
uniform sampler2D u_sky_night_tex; // night tex
uniform vec3      u_tint_a;
uniform vec3      u_tint_b;
uniform int       u_flip_a;
uniform int       u_flip_b;
uniform int       u_use_night_b;   // 1 = sample night tex for B side
uniform float     u_blend;
uniform float     u_uv_offset;
const float PI = 3.14159265;
void main(){
    vec4 world = u_inv_view_proj * vec4(v_ndc, 1.0, 1.0);
    vec3 dir = normalize(world.xyz / world.w);
    float base_u = (atan(dir.z, dir.x) / (2.0 * PI)) + 0.5 + u_uv_offset;
    float v = 1.0 - (asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5);

    float u_a = (u_flip_a == 1) ? (1.0 - base_u) : base_u;
    float u_b = (u_flip_b == 1) ? (1.0 - base_u) : base_u;

    vec3 col_a = texture(u_sky_tex, vec2(u_a, v)).rgb * u_tint_a;
    vec3 col_b = (u_use_night_b == 1)
        ? texture(u_sky_night_tex, vec2(u_b, v)).rgb * u_tint_b
        : texture(u_sky_tex, vec2(u_b, v)).rgb * u_tint_b;
    frag_color = vec4(mix(col_a, col_b, u_blend), 1.0);
}
)";

static const char* DEPTH_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_light_space;
uniform mat4 u_model;
void main(){
    gl_Position = u_light_space * u_model * vec4(a_pos, 1.0);
}
)";

static const char* DEPTH_FRAG_SRC = R"(
#version 330 core
void main(){}
)";


// internal helpers

static void set_vec3(const Shader& s, const char* n, glm::vec3 v){
    glUniform3f(glGetUniformLocation(s.id, n), v.x, v.y, v.z);
}
static void set_mat4(const Shader& s, const char* n, const glm::mat4& m){
    glUniformMatrix4fv(glGetUniformLocation(s.id, n), 1, GL_FALSE, glm::value_ptr(m));
}
static void set_mat3(const Shader& s, const char* n, const glm::mat3& m){
    glUniformMatrix3fv(glGetUniformLocation(s.id, n), 1, GL_FALSE, glm::value_ptr(m));
}

// uploads and draws a throwaway wire AABB then destroys it
// debug only
static void draw_wire_aabb(
    const Shader& gizmo_shader,
    const Aabb& box,
    glm::vec3 color,
    const glm::mat4& view,
    const glm::mat4& proj)
{
    glm::vec3 lo = box.min, hi = box.max;
    glm::vec3 c[8] = {
        {lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},
        {hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},
        {lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},
        {hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z},
    };
    int e[24] = { 0,1,1,2,2,3,3,0, 4,5,5,6,6,7,7,4, 0,4,1,5,2,6,3,7 };

    std::vector<float> v;
    for (int i = 0; i < 24; i += 2){
        glm::vec3 a = c[e[i]], b = c[e[i+1]];
        v.insert(v.end(), {a.x,a.y,a.z, color.r,color.g,color.b});
        v.insert(v.end(), {b.x,b.y,b.z, color.r,color.g,color.b});
    }

    Mesh wire;
    mesh_init(wire, v);
    shader_bind(gizmo_shader);
    set_mat4(gizmo_shader, "u_view", view);
    set_mat4(gizmo_shader, "u_proj", proj);
    set_mat4(gizmo_shader, "u_model", glm::mat4(1.0f));
    glBindVertexArray(wire.vao);
    glDrawArrays(GL_LINES, 0, wire.count);
    glBindVertexArray(0);
    mesh_destroy(wire);
}

// builds and draws a lit box using push_box_lit (pos+normal layout)
// used for obstacle solid mesh
static void draw_box_lit(
    const Shader& shader,
    glm::vec3 center_bottom,
    glm::vec3 full_size,
    glm::vec3 color,
    const glm::mat4& view,
    const glm::mat4& proj)
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), center_bottom);
    glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(m)));

    set_mat4(shader, "u_model", m);
    set_mat3(shader, "u_normal_mat", nm);
    set_vec3(shader, "u_kd", color);

    std::vector<float> verts;
    push_box_lit(verts, glm::vec3(0.0f), full_size, color);

    Mesh box;
    mesh_init(box, verts);
    glBindVertexArray(box.vao);
    glDrawArrays(GL_TRIANGLES, 0, box.count);
    glBindVertexArray(0);
    mesh_destroy(box);
}

void scene_init(SceneState& scene){

    shader_init(scene.shader, VERT_SRC, FRAG_SRC);
    shader_init(scene.gizmo_shader, GIZMO_VERT_SRC, GIZMO_FRAG_SRC);

    // skybox
    shader_init(scene.sky_shader, SKY_VERT_SRC, SKY_FRAG_SRC);
    {
        // fullscreen triangle-pair quad, NDC coords only, no normals needed
        std::vector<float> sq = {
            -1,-1,  1,-1,  1, 1,
            -1,-1,  1, 1, -1, 1
        };
        GLuint vao, vbo;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sq.size()*sizeof(float), sq.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
        scene.sky_quad.vao = vao;
        scene.sky_quad.vbo = vbo;
        scene.sky_quad.count = 6;
    }

    // load sky texture
    if (Const::SKY_IMAGE_PATH[0] != '\0'){
        stbi_set_flip_vertically_on_load(0); // equirectangular: no flip
        int w, h, ch;
        unsigned char* px = stbi_load(Const::SKY_IMAGE_PATH, &w, &h, &ch, 3);
        if (px){
            glGenTextures(1, &scene.sky_tex);
            glBindTexture(GL_TEXTURE_2D, scene.sky_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            stbi_image_free(px);
            std::cout << "[sky] loaded " << w << "x" << h << " " << Const::SKY_IMAGE_PATH << "\n";
        } else {
            std::cerr << "[sky] failed to load: " << Const::SKY_IMAGE_PATH << "\n";
        }
    }


     // load night sky
    {
        const char* night_path = "../assets/sky_night.jpg";
        stbi_set_flip_vertically_on_load(0);
        int w, h, ch;
        unsigned char* px = stbi_load(night_path, &w, &h, &ch, 3);
        if (px){
            glGenTextures(1, &scene.sky_night_tex);
            glBindTexture(GL_TEXTURE_2D, scene.sky_night_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            stbi_image_free(px);
            std::cout << "[sky] loaded night " << w << "x" << h << "\n";
        }
    }


    // shadow map FBO
    shader_init(scene.shadow_shader, DEPTH_VERT_SRC, DEPTH_FRAG_SRC);
    glGenFramebuffers(1, &scene.shadow_fbo);
    glGenTextures(1, &scene.shadow_depth_tex);
    glBindTexture(GL_TEXTURE_2D, scene.shadow_depth_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
        Const::SHADOW_MAP_SIZE, Const::SHADOW_MAP_SIZE,
        0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindFramebuffer(GL_FRAMEBUFFER, scene.shadow_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D, scene.shadow_depth_tex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // toggle between using proc mesh or a OBJ model
    // I'll use it for debugging purposes
    // I want to get the physics close to "realistic" because
    // at the moment still has some shitty issues that I can't scratch my head around
    if constexpr (Const::USE_PROC_MESH){
        std::vector<float> proc_verts;
        build_tricycle_mesh(proc_verts);
        mesh_init(scene.proc_mesh, proc_verts);

        scene.model_center = glm::vec3(0.0f);
        scene.model_scale = 1.0f;
        scene.model_half_height = 0.625f;
    } 
    else {
        trike_model_init(scene.trike_model);
        driver_model_init(scene.driver_model);

        const auto& verts = scene.trike_model.mesh.data.vertices;

        float minX=1e9f,maxX=-1e9f,minY=1e9f,maxY=-1e9f,minZ=1e9f,maxZ=-1e9f;
        for (int i = 0; i < (int)verts.size(); i += 8){
            float x = verts[i];
            float y = verts[i+1];
            float z = verts[i+2];
            minX=std::min(minX,x); maxX=std::max(maxX,x);
            minY=std::min(minY,y); maxY=std::max(maxY,y);
            minZ=std::min(minZ,z); maxZ=std::max(maxZ,z);
        }

        scene.model_center = glm::vec3(
            (minX+maxX)*0.5f, minY, (minZ+maxZ)*0.5f);
        scene.model_center.y -= Const::MODEL_FLOOR_FUDGE;

        float longest = std::max(maxX-minX, std::max(maxY-minY, maxZ-minZ));
        scene.model_scale = (longest > 0.0f) ? Const::MODEL_NORMALIZE_SIZE / longest : 1.0f;
        scene.model_half_height = (maxY-minY) * 0.5f * scene.model_scale;
    }


    // ground
    std::vector<float> gv;
    push_ground_quad(gv, Const::GROUND_HALF_EXTENT);
    mesh_init(scene.ground, gv);

    // axis gizmo
    std::vector<float> av;
    push_axis_gizmo(av, Const::GIZMO_LENGTH);
    mesh_init(scene.gizmo, av);

    // CACHE UNIFORMS FOR LOC ONCE
    {
        GLuint id = scene.shader.id;
        auto& L = scene.shader_loc;
        L.view = glGetUniformLocation(id, "u_view");
        L.proj = glGetUniformLocation(id, "u_proj");
        L.model = glGetUniformLocation(id, "u_model");
        L.normal_mat = glGetUniformLocation(id, "u_normal_mat");
        L.light_dir = glGetUniformLocation(id, "u_light_dir");
        L.light_color = glGetUniformLocation(id, "u_light_color");
        L.light_space = glGetUniformLocation(id, "u_light_space");
        L.ambient = glGetUniformLocation(id, "u_ambient");
        L.diff_intensity = glGetUniformLocation(id, "u_diff_intensity");
        L.shadow_bias = glGetUniformLocation(id, "u_shadow_bias");
        L.shadow_map = glGetUniformLocation(id, "u_shadow_map");
        L.kd = glGetUniformLocation(id, "u_kd");
        L.kd_alt = glGetUniformLocation(id, "u_kd_alt");
        L.checker_scale = glGetUniformLocation(id, "u_checker_scale");
        L.use_checker = glGetUniformLocation(id, "u_use_checker");
    }
    {
        GLuint id = scene.gizmo_shader.id;
        auto& L = scene.gizmo_loc;
        L.view  = glGetUniformLocation(id, "u_view");
        L.proj  = glGetUniformLocation(id, "u_proj");
        L.model = glGetUniformLocation(id, "u_model");
    }
    {
        GLuint id = scene.sky_shader.id;
        auto& L = scene.sky_loc;
        L.inv_view_proj = glGetUniformLocation(id, "u_inv_view_proj");
        L.sky_tex = glGetUniformLocation(id, "u_sky_tex");
        L.sky_night_tex = glGetUniformLocation(id, "u_sky_night_tex");
        L.tint_a = glGetUniformLocation(id, "u_tint_a");
        L.tint_b = glGetUniformLocation(id, "u_tint_b");
        L.flip_a = glGetUniformLocation(id, "u_flip_a");
        L.flip_b = glGetUniformLocation(id, "u_flip_b");
        L.blend = glGetUniformLocation(id, "u_blend");
        L.uv_offset = glGetUniformLocation(id, "u_uv_offset");
        L.use_night_b = glGetUniformLocation(id, "u_use_night_b");
    }
    {
        GLuint id = scene.shadow_shader.id;
        scene.shadow_loc.light_space = glGetUniformLocation(id, "u_light_space");
        scene.shadow_loc.model = glGetUniformLocation(id, "u_model");
    }
    {
        GLuint id = scene.shader.id;
        auto& LL = scene.light_loc;
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

    // persistent line batch for hitbox wireframes
    std::vector<float> empty(6, 0.0f);
    mesh_init(scene.line_batch, empty);
    scene.line_verts.reserve(128 * 24 * 6);
}

void scene_shadow_pass(SceneState& scene, const std::vector<Obstacle>& obstacles, glm::vec3 center){
    float texel_size = (2.0f * Const::SHADOW_ORTHO_SIZE) / (float)Const::SHADOW_MAP_SIZE;
    glm::vec3 snapped = glm::vec3(
        std::floor(center.x / texel_size) * texel_size,
        center.y,
        std::floor(center.z / texel_size) * texel_size);

    glm::vec3 light_pos = snapped + scene.sun_dir * 150.0f;
    glm::mat4 light_view = glm::lookAt(light_pos, snapped, glm::vec3(0,1,0));
    float s = Const::SHADOW_ORTHO_SIZE;
    scene.light_space_mat = glm::ortho(-s, s, -s, s, Const::SHADOW_NEAR, Const::SHADOW_FAR) * light_view;
}

void scene_trike_shadow_draw(SceneState& scene, const TrikeState& trike){
    glm::vec3 render_pos = trike.position;
    if (trike.is_tipping)
        render_pos.y = scene.model_half_height * std::abs(std::cos(trike.roll_angle));
    else if (trike.is_rolled_over)
        render_pos.y = 0.0f;

    glm::vec3 sc = scene.model_center * scene.model_scale;
    glm::mat4 tm =
        glm::translate(glm::mat4(1.0f), render_pos)
        * glm::rotate(glm::mat4(1.0f), -trike.heading, glm::vec3(0,1,0))
        * glm::rotate(glm::mat4(1.0f), -trike.roll_angle, glm::vec3(1,0,0))
        * glm::rotate(glm::mat4(1.0f), glm::radians(Const::TRIKE_MODEL_YAW_OFFSET), glm::vec3(0,1,0))
        * glm::translate(glm::mat4(1.0f), -sc)
        * glm::scale(glm::mat4(1.0f), glm::vec3(scene.model_scale));

    shader_bind(scene.shadow_shader);
    glUniformMatrix4fv(scene.shadow_loc.light_space, 1, GL_FALSE, glm::value_ptr(scene.light_space_mat));
    glUniformMatrix4fv(scene.shadow_loc.model, 1, GL_FALSE, glm::value_ptr(tm));

    if constexpr (Const::USE_PROC_MESH){
        glBindVertexArray(scene.proc_mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, scene.proc_mesh.count);
        glBindVertexArray(0);
    } 
    else {
        ObjMesh& mesh = scene.trike_model.mesh;
        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(mesh.data.vertices.size() / 8));
        glBindVertexArray(0);
    }
}

void scene_update_daytime(SceneState& scene, float dt){
    // advance time — 10 real minutes = 24 in-game hours
    float time_scale = 24.0f / Const::DAY_DURATION_SECONDS;
    scene.day_time += dt * time_scale;
    if (scene.day_time >= 24.0f) scene.day_time -= 24.0f;

    float t = scene.day_time;

    // sun elevation: rises at 5am, peaks at noon, sets at 19:00
    // map t=5 -> 0 deg, t=12 -> 90 deg, t=19 -> 0 deg
    // use a sine over the day window
    float day_frac = glm::clamp((t - 5.0f) / 14.0f, 0.0f, 1.0f); // 0 at 5am, 1 at 7pm
    float elevation = glm::pi<float>() * day_frac; // 0 -> pi
    float sun_y = std::sin(elevation);
    // azimuth rotates east(morning) to west(evening)
    float azimuth = glm::pi<float>() * day_frac; // east at dawn, west at dusk
    float sun_x = std::cos(azimuth);
    float sun_z = 0.3f; // slight south offset for our lat

    // at night use a dim moon direction
    bool is_night = (t < 5.0f || t >= 19.0f);
    if (is_night){
        scene.sun_dir = glm::normalize(glm::vec3(0.3f, 0.5f, 0.2f));
    } 
    else {
        scene.sun_dir = glm::normalize(glm::vec3(sun_x, sun_y, sun_z));
    }

    // determine which two periods we're blending between
    // period A = current, period B = next, blend = 0->1 over FADE_DURATION hours
    glm::vec3 col_morning = {Const::LIGHT_MORNING_R,   Const::LIGHT_MORNING_G,   Const::LIGHT_MORNING_B};
    glm::vec3 col_afternoon = {Const::LIGHT_AFTERNOON_R, Const::LIGHT_AFTERNOON_G, Const::LIGHT_AFTERNOON_B};
    glm::vec3 col_night = {Const::LIGHT_NIGHT_R,     Const::LIGHT_NIGHT_G,     Const::LIGHT_NIGHT_B};

    float amb_morning = Const::LIGHT_MORNING_AMBIENT;
    float amb_afternoon = Const::LIGHT_AFTERNOON_AMBIENT;
    float amb_night = Const::LIGHT_NIGHT_AMBIENT;
    float diff_morning = Const::LIGHT_MORNING_DIFF;
    float diff_afternoon= Const::LIGHT_AFTERNOON_DIFF;
    float diff_night = Const::LIGHT_NIGHT_DIFF;

    float fade = Const::DAY_FADE_DURATION;

    auto blend_f = [](float edge, float t, float fade) -> float {
        return glm::clamp((t - edge) / fade, 0.0f, 1.0f);
    };

    if (t >= Const::DAY_MORNING_START && t < Const::DAY_AFTERNOON_START){
        float b = glm::clamp((t - Const::DAY_MORNING_START) /
            (Const::DAY_AFTERNOON_START - Const::DAY_MORNING_START), 0.0f, 1.0f);
        scene.sky_uv_offset = glm::mix(0.0f, 0.25f, b); // slowly drifts east to south
    } 
    else if (t >= Const::DAY_AFTERNOON_START && t < Const::DAY_NIGHT_START){
        float b = glm::clamp((t - Const::DAY_AFTERNOON_START) /
            (Const::DAY_NIGHT_START - Const::DAY_AFTERNOON_START), 0.0f, 1.0f);
        scene.sky_uv_offset = glm::mix(0.25f, 0.50f, b); // south to west
    } 
    else {
        scene.sky_uv_offset = 0.0f;
    }

    if (t >= Const::DAY_NIGHT_START){
        // afternoon -> night: day flipped+orange fades to sky_night
        float b = blend_f(Const::DAY_NIGHT_START, t, fade);
        scene.light_color = glm::mix(col_afternoon, col_night, b);
        scene.ambient = glm::mix(amb_afternoon, amb_night, b);
        scene.diff_intensity = glm::mix(diff_afternoon, diff_night, b);
        scene.sky_tint_a = glm::vec3(1.0f, 0.55f, 0.25f); // golden afternoon
        scene.sky_tint_b = glm::vec3(1.0f);
        scene.sky_flip_a = 1;
        scene.sky_flip_b = 0;
        scene.sky_use_night_b = 1;
        scene.sky_blend = b;
    }
    else if (t >= Const::DAY_AFTERNOON_START){
        // morning -> afternoon: day normal fades to day flipped+orange
        float b = blend_f(Const::DAY_AFTERNOON_START, t, fade);
        scene.light_color = glm::mix(col_morning, col_afternoon, b);
        scene.ambient = glm::mix(amb_morning, amb_afternoon, b);
        scene.diff_intensity = glm::mix(diff_morning, diff_afternoon, b);
        scene.sky_tint_a = glm::vec3(1.0f); // normal day
        scene.sky_tint_b = glm::vec3(1.0f, 0.55f, 0.25f); // golden tint
        scene.sky_flip_a = 0;
        scene.sky_flip_b = 1;
        scene.sky_use_night_b = 0;
        scene.sky_blend = b;
    }
    else if (t >= Const::DAY_MORNING_START){
        // night -> morning: sky_night fades to day
        float b = blend_f(Const::DAY_MORNING_START, t, fade);
        scene.light_color = glm::mix(col_night, col_morning, b);
        scene.ambient = glm::mix(amb_night, amb_morning, b);
        scene.diff_intensity = glm::mix(diff_night, diff_morning, b);
        scene.sky_tint_a = glm::vec3(1.0f); // night tex as-is
        scene.sky_tint_b = glm::vec3(1.0f); // day tex
        scene.sky_flip_a = 0;
        scene.sky_flip_b = 0;
        scene.sky_use_night_b = 0; // A=night B=day, but A uses night tex
        scene.sky_use_night_b = 0;
        scene.sky_tint_a = glm::vec3(1.0f); // day
        scene.sky_tint_b = glm::vec3(1.0f); // night
        scene.sky_flip_a = 0;
        scene.sky_flip_b = 0;
        scene.sky_use_night_b = 1; // B=night, blend goes 1->0 (night fades out)
        scene.sky_blend = 1.0f - b; // inverted: starts at night, fades to day
    }
    else {
        // deep night
        scene.light_color = col_night;
        scene.ambient = amb_night;
        scene.diff_intensity = diff_night;
        scene.sky_tint_a = glm::vec3(1.0f);
        scene.sky_tint_b = glm::vec3(1.0f);
        scene.sky_flip_a = 0;
        scene.sky_flip_b = 0;
        scene.sky_use_night_b = 1;
        scene.sky_blend = 1.0f; // fully night
    }

    // night_factor: 0 at peak day (noon), 1 at full night
    // ramps up from DAY_AFTERNOON_START, fully on by DAY_NIGHT_START
    // ramps down from DAY_MORNING_START, fully off by DAY_MORNING_START + fade
    float nf = 0.0f;
    if (t >= Const::DAY_NIGHT_START)
        nf = glm::clamp((t - Const::DAY_NIGHT_START) / Const::DAY_FADE_DURATION, 0.0f, 1.0f);
    else if (t < Const::DAY_MORNING_START)
        nf = 1.0f;
    else if (t < Const::DAY_MORNING_START + Const::DAY_FADE_DURATION)
        nf = 1.0f - glm::clamp((t - Const::DAY_MORNING_START) / Const::DAY_FADE_DURATION, 0.0f, 1.0f);
    scene.night_factor = nf;

    static float s_last_printed = -1.0f;
    if ((int)scene.day_time != (int)s_last_printed){
        std::cout << "[day] t=" << scene.day_time << " night_factor=" << nf << "\n";
        s_last_printed = scene.day_time;
    }



}



void scene_draw_sky(SceneState& scene, const glm::mat4& view, const glm::mat4& proj){
    if (!scene.sky_tex) return;

    glm::mat4 rot_only = glm::mat4(glm::mat3(view));
    glm::mat4 inv_vp = glm::inverse(proj * rot_only);

    auto& SL = scene.sky_loc;
    shader_bind(scene.sky_shader);
    glUniformMatrix4fv(SL.inv_view_proj, 1, GL_FALSE, glm::value_ptr(inv_vp));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene.sky_tex);
    glUniform1i(SL.sky_tex, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, scene.sky_night_tex ? scene.sky_night_tex : scene.sky_tex);
    glUniform1i(SL.sky_night_tex, 1);
    glActiveTexture(GL_TEXTURE0);
    glUniform3f(SL.tint_a, scene.sky_tint_a.r, scene.sky_tint_a.g, scene.sky_tint_a.b);
    glUniform3f(SL.tint_b, scene.sky_tint_b.r, scene.sky_tint_b.g, scene.sky_tint_b.b);
    glUniform1i(SL.flip_a, scene.sky_flip_a);
    glUniform1i(SL.flip_b, scene.sky_flip_b);
    glUniform1f(SL.blend, scene.sky_blend);
    glUniform1f(SL.uv_offset, scene.sky_uv_offset);
    glUniform1i(SL.use_night_b, scene.sky_use_night_b);
    
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(scene.sky_quad.vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void scene_draw(
    SceneState& scene,
    const TrikeState& trike,
    const std::vector<Obstacle>& obstacles,
    const std::vector<LightSource>& lights,
    const glm::mat4& view,
    const glm::mat4& proj,
    bool show_hitboxes)
{
    // ground
    glm::mat4 gm = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, Const::GROUND_Y_OFFSET, 0.0f));
    glm::mat3 gnm = glm::mat3(1.0f);

    auto& L = scene.shader_loc;
    shader_bind(scene.shader);
    glUniformMatrix4fv(L.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(L.proj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform3f(L.light_dir, scene.sun_dir.x, scene.sun_dir.y, scene.sun_dir.z);
    glUniformMatrix4fv(L.light_space, 1, GL_FALSE, glm::value_ptr(scene.light_space_mat));
    glUniform3f(L.light_color, scene.light_color.r, scene.light_color.g, scene.light_color.b);
    glUniform1f(L.ambient, scene.ambient);
    glUniform1f(L.diff_intensity, scene.diff_intensity);
    glUniform1f(L.shadow_bias, Const::SHADOW_BIAS);

    // upload point lights 
    // cpu cull to camera distance
    glm::vec3 cam_pos = glm::vec3(glm::inverse(view)[3]);
    int active_lcount = 0;
    if (scene.night_factor >= 0.01f){
        for (int i = 0; i < (int)lights.size() && active_lcount < Const::MAX_POINT_LIGHTS; i++){
            glm::vec3 d = lights[i].position - cam_pos;
            if (glm::dot(d, d) > Const::LIGHT_CULL_DIST_SQ) continue;
            glUniform3f(scene.light_loc.pos[active_lcount], lights[i].position.x, lights[i].position.y, lights[i].position.z);
            glUniform3f(scene.light_loc.color[active_lcount], lights[i].color.r, lights[i].color.g, lights[i].color.b);
            glUniform1f(scene.light_loc.radius[active_lcount], lights[i].radius);
            glUniform1f(scene.light_loc.intensity[active_lcount], lights[i].intensity * scene.night_factor);
            active_lcount++;
        }
    }
    glUniform1i(scene.light_loc.count, active_lcount);


    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, scene.shadow_depth_tex);
    glUniform1i(L.shadow_map, 1);
    glActiveTexture(GL_TEXTURE0);

    glUniformMatrix4fv(L.model, 1, GL_FALSE, glm::value_ptr(gm));
    glUniformMatrix3fv(L.normal_mat, 1, GL_FALSE, glm::value_ptr(gnm));
    glUniform3f(L.kd, Const::GROUND_KD, Const::GROUND_KD, Const::GROUND_KD);
    glUniform3f(L.kd_alt, Const::GROUND_KD_ALT, Const::GROUND_KD_ALT, Const::GROUND_KD_ALT);
    glUniform1f(L.checker_scale, 1.0f / Const::GROUND_GRID_TILE_SIZE);
    glUniform1i(L.use_checker, 0);

    // axis gizmo
    /*shader_bind(scene.gizmo_shader);
    set_mat4(scene.gizmo_shader, "u_view", view);
    set_mat4(scene.gizmo_shader, "u_proj", proj);
    set_mat4(scene.gizmo_shader, "u_model", glm::mat4(1.0f));
    glBindVertexArray(scene.gizmo.vao);
    glDrawArrays(GL_LINES, 0, scene.gizmo.count);
    glBindVertexArray(0);*/

    // trike 
    glm::vec3 render_pos = trike.position;
    if (trike.is_tipping) render_pos.y = scene.model_half_height * std::abs(std::cos(trike.roll_angle));

    else if (trike.is_rolled_over) render_pos.y = 0.0f;

    glm::vec3 sc = scene.model_center * scene.model_scale;
    glm::mat4 tm =
        glm::translate(glm::mat4(1.0f), render_pos)
        * glm::rotate(glm::mat4(1.0f), -trike.heading, glm::vec3(0,1,0))
        * glm::rotate(glm::mat4(1.0f), -trike.roll_angle, glm::vec3(1,0,0))
        * glm::rotate(glm::mat4(1.0f), glm::radians(Const::TRIKE_MODEL_YAW_OFFSET), glm::vec3(0,1,0))
        * glm::translate(glm::mat4(1.0f), -sc)
        * glm::scale(glm::mat4(1.0f), glm::vec3(scene.model_scale));

    glm::mat3 tnm = glm::mat3(tm);

    glUniformMatrix4fv(L.model, 1, GL_FALSE, glm::value_ptr(tm));
    glUniformMatrix3fv(L.normal_mat, 1, GL_FALSE, glm::value_ptr(tnm));

    if constexpr (Const::USE_PROC_MESH){
        // proc mesh uses rgb color layout not normals 
        // for now I'll draw with gizmo shader
        // lighting won't apply but colors are baked per face in mesh_builder
        
        shader_bind(scene.gizmo_shader);
        glUniformMatrix4fv(scene.gizmo_loc.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(scene.gizmo_loc.proj, 1, GL_FALSE, glm::value_ptr(proj));
        glUniformMatrix4fv(scene.gizmo_loc.model,1, GL_FALSE, glm::value_ptr(tm));
        glBindVertexArray(scene.proc_mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, scene.proc_mesh.count);
        glBindVertexArray(0);
        shader_bind(scene.shader);
    } 
    else {
        trike_model_draw(scene.trike_model, trike, scene.shader, view, proj);
    }

    // cube meshes not drawn
    // world objects are rendered as OBJ meshes via editor_renderer_draw_props
    // commented for now instead of removed for further testing
    /*
    // obstacle solid meshes
    // flashes red on hit
    shader_bind(scene.shader);
    set_mat4(scene.shader, "u_view", view);
    set_mat4(scene.shader, "u_proj", proj);
    set_vec3(scene.shader, "u_light_dir", LIGHT_DIR);
    glUniform1i(glGetUniformLocation(scene.shader.id, "u_use_checker"), 0);

    for (const auto& obs : obstacles){
        float flash = glm::clamp(obs.hit_timer / 0.35f, 0.0f, 1.0f);
        glm::vec3 base = {0.45f, 0.43f, 0.40f};
        glm::vec3 hit = {0.9f, 0.15f, 0.10f};
        glm::vec3 color = glm::mix(base, hit, flash);

        glm::vec3 cb = glm::vec3(
            (obs.aabb.min.x + obs.aabb.max.x) * 0.5f,
             obs.aabb.min.y,
            (obs.aabb.min.z + obs.aabb.max.z) * 0.5f);

        draw_box_lit(scene.shader, cb, obs.half_extents * 2.0f, color, view, proj);
    }*/

    // AABB wireframes
    // green=trike, 
    // yellow=obstacles
    if (show_hitboxes){
        scene.line_verts.clear();
        auto push_aabb = [&](const Aabb& box, glm::vec3 col){
            glm::vec3 lo = box.min, hi = box.max;
            glm::vec3 c[8] = {
                {lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},
                {hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},
                {lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},
                {hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z},
            };
            int e[24] = { 0,1,1,2,2,3,3,0, 4,5,5,6,6,7,7,4, 0,4,1,5,2,6,3,7 };
            for (int i = 0; i < 24; i++){
                glm::vec3 p = c[e[i]];
                scene.line_verts.insert(scene.line_verts.end(),
                    {p.x,p.y,p.z, col.r,col.g,col.b});
            }
        };

        push_aabb(trike.aabb, {0.0f,1.0f,0.3f});
        for (const auto& obs : obstacles)
            push_aabb(obs.aabb, {1.0f,0.9f,0.0f});

        glBindBuffer(GL_ARRAY_BUFFER, scene.line_batch.vbo);
        glBufferData(GL_ARRAY_BUFFER,
            scene.line_verts.size() * sizeof(float),
            scene.line_verts.data(), GL_DYNAMIC_DRAW);
        scene.line_batch.count = (int)scene.line_verts.size() / 6;

        glm::mat4 identity = glm::mat4(1.0f);
        shader_bind(scene.gizmo_shader);
        glUniformMatrix4fv(scene.gizmo_loc.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(scene.gizmo_loc.proj, 1, GL_FALSE, glm::value_ptr(proj));
        glUniformMatrix4fv(scene.gizmo_loc.model, 1, GL_FALSE, glm::value_ptr(identity));
        glBindVertexArray(scene.line_batch.vao);
        glDrawArrays(GL_LINES, 0, scene.line_batch.count);
        glBindVertexArray(0);
    }

    shader_bind(scene.shader);
}

void scene_draw_driver(
     SceneState& scene,
     const PlayerState& player,
     const TrikeState& trike,
     const glm::mat4& view,
     const glm::mat4& proj,
     const Shader& lit_shader,
     const glm::quat  pose_quats[BONE_COUNT],
     const glm::vec3  pose_offsets[BONE_COUNT],
     glm::vec3 pose_seat)
 {
     shader_bind(lit_shader);
     driver_model_draw(scene.driver_model, player, trike, lit_shader, view, proj,
         pose_quats, pose_offsets, pose_seat);
 }


void scene_draw_drop_marker(SceneState& scene, glm::vec3 pos, float pulse, const glm::mat4& view, const glm::mat4& proj){
    // glowing ring as line loop around drop point
    static constexpr int SEGMENTS = 24;
    static constexpr float RADIUS = 1.5f;
    float r = RADIUS * pulse;

    std::vector<float> verts;
    verts.reserve(SEGMENTS * 2 * 6);

    // pulsing yellow-green color
    float t = (float)glfwGetTime();
    float glow = 0.7f + 0.3f * std::sin(t * 4.0f);
    glm::vec3 col = {0.3f, 1.0f * glow, 0.2f};

    for (int i = 0; i < SEGMENTS; i++){
        float a0 = (float)i       / SEGMENTS * glm::two_pi<float>();
        float a1 = (float)(i + 1) / SEGMENTS * glm::two_pi<float>();
        glm::vec3 p0 = pos + glm::vec3(std::cos(a0) * r, 0.05f, std::sin(a0) * r);
        glm::vec3 p1 = pos + glm::vec3(std::cos(a1) * r, 0.05f, std::sin(a1) * r);
        verts.insert(verts.end(), {p0.x, p0.y, p0.z, col.r, col.g, col.b});
        verts.insert(verts.end(), {p1.x, p1.y, p1.z, col.r, col.g, col.b});
    }

    // second ring elevated
    float up = 1.2f * pulse;
    for (int i = 0; i < SEGMENTS; i++){
        float a0 = (float)i       / SEGMENTS * glm::two_pi<float>();
        float a1 = (float)(i + 1) / SEGMENTS * glm::two_pi<float>();
        glm::vec3 p0 = pos + glm::vec3(std::cos(a0) * r, up, std::sin(a0) * r);
        glm::vec3 p1 = pos + glm::vec3(std::cos(a1) * r, up, std::sin(a1) * r);
        verts.insert(verts.end(), {p0.x, p0.y, p0.z, col.r, col.g, col.b});
        verts.insert(verts.end(), {p1.x, p1.y, p1.z, col.r, col.g, col.b});
    }

    // vertical bars connecting the two rings
    for (int i = 0; i < SEGMENTS; i += 3){
        float a = (float)i / SEGMENTS * glm::two_pi<float>();
        glm::vec3 bot = pos + glm::vec3(std::cos(a) * r, 0.05f, std::sin(a) * r);
        glm::vec3 top = pos + glm::vec3(std::cos(a) * r, up,    std::sin(a) * r);
        verts.insert(verts.end(), {bot.x, bot.y, bot.z, col.r, col.g, col.b});
        verts.insert(verts.end(), {top.x, top.y, top.z, col.r, col.g, col.b});
    }

    glBindBuffer(GL_ARRAY_BUFFER, scene.line_batch.vbo);
    glBufferData(GL_ARRAY_BUFFER,
        verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    scene.line_batch.count = (int)verts.size() / 6;

    glm::mat4 identity = glm::mat4(1.0f);
    shader_bind(scene.gizmo_shader);
    glUniformMatrix4fv(scene.gizmo_loc.view,  1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(scene.gizmo_loc.proj,  1, GL_FALSE, glm::value_ptr(proj));
    glUniformMatrix4fv(scene.gizmo_loc.model, 1, GL_FALSE, glm::value_ptr(identity));
    glBindVertexArray(scene.line_batch.vao);
    glDrawArrays(GL_LINES, 0, scene.line_batch.count);
    glBindVertexArray(0);
}

void scene_destroy(SceneState& scene){
    if (scene.sky_tex) glDeleteTextures(1, &scene.sky_tex);
    if (scene.sky_night_tex) glDeleteTextures(1, &scene.sky_night_tex);
    shader_destroy(scene.sky_shader);
    mesh_destroy(scene.sky_quad);
    shader_destroy(scene.shadow_shader);
    if (scene.shadow_fbo) glDeleteFramebuffers(1, &scene.shadow_fbo);
    if (scene.shadow_depth_tex) glDeleteTextures(1, &scene.shadow_depth_tex);
    trike_model_destroy(scene.trike_model);
    obj_mesh_destroy(scene.trike_mesh);
    mesh_destroy(scene.proc_mesh);
    mesh_destroy(scene.ground);
    mesh_destroy(scene.gizmo);
    shader_destroy(scene.shader);
    shader_destroy(scene.gizmo_shader);
}