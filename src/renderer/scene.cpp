#include "scene.hpp"
#include "mesh_builder.hpp"
#include "obj_loader.hpp"
#include "../core/const.hpp"
#include "../physics/trike_aabb.hpp"
#include <glad/glad.h>
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
out vec3 v_world_normal;
out vec3 v_world_pos;
void main(){
    vec4 world = u_model * vec4(a_pos, 1.0);
    gl_Position = u_proj * u_view * world;
    v_world_normal = normalize(u_normal_mat * a_normal);
    v_world_pos = world.xyz;
}
)";

static const char* FRAG_SRC = R"(
#version 330 core
in  vec3 v_world_normal;
in  vec3 v_world_pos;
out vec4 frag_color;
uniform vec3  u_kd;
uniform vec3  u_kd_alt;
uniform vec3  u_light_dir;
uniform float u_checker_scale;
uniform int   u_use_checker;
void main(){
    float diff = max(dot(normalize(v_world_normal), u_light_dir), 0.0);
    float ambient = 0.55;
    vec3 lit = u_kd * (ambient + diff * 0.85);
    if (u_use_checker == 1){
        vec2 tile = floor(v_world_pos.xz * u_checker_scale);
        float parity = mod(tile.x + tile.y, 2.0);
        vec3  kd = mix(u_kd, u_kd_alt, parity);
        lit = kd * (ambient + diff * 0.85);
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
uniform mat4 u_inv_view_proj;
uniform sampler2D u_sky_tex;
const float PI = 3.14159265;
void main(){
    vec4 world = u_inv_view_proj * vec4(v_ndc, 1.0, 1.0);
    vec3 dir = normalize(world.xyz / world.w);
    float u = (atan(dir.z, dir.x) / (2.0 * PI)) + 0.5;
    float v = 1.0 - (asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5);
    frag_color = texture(u_sky_tex, vec2(u, v));
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
static const glm::vec3 LIGHT_DIR = glm::normalize(
    glm::vec3(Const::LIGHT_DIR_X, Const::LIGHT_DIR_Y, Const::LIGHT_DIR_Z));

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
}

void scene_shadow_pass(SceneState& scene, const std::vector<Obstacle>& obstacles, glm::vec3 center){
    // build light space matrix from sun dir
    // orthographic projection looking from sun toward scene center
    glm::vec3 light_dir = glm::normalize(
        glm::vec3(Const::LIGHT_DIR_X, Const::LIGHT_DIR_Y, Const::LIGHT_DIR_Z));

    glm::vec3 light_pos = center + light_dir * 150.0f;
    glm::mat4 light_view = glm::lookAt(light_pos, center, glm::vec3(0,1,0));


    float s = Const::SHADOW_ORTHO_SIZE;
    glm::mat4 light_proj = glm::ortho(-s, s, -s, s, Const::SHADOW_NEAR, Const::SHADOW_FAR);
    scene.light_space_mat = light_proj * light_view;

    glBindFramebuffer(GL_FRAMEBUFFER, scene.shadow_fbo);
    glViewport(0, 0, Const::SHADOW_MAP_SIZE, Const::SHADOW_MAP_SIZE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // front-face culling during shadow pass eliminates peter-panning on back faces
    glCullFace(GL_FRONT);
    glEnable(GL_CULL_FACE);

    shader_bind(scene.shadow_shader);
    glUniformMatrix4fv(glGetUniformLocation(scene.shadow_shader.id, "u_light_space"),
        1, GL_FALSE, glm::value_ptr(scene.light_space_mat));

    // render trike into shadow map
    glm::mat4 identity = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(scene.shadow_shader.id, "u_model"),
        1, GL_FALSE, glm::value_ptr(identity));

    // obstacles (static world geometry) cast shadows too
    // just need pos+layout attrib 0, depth shader ignores normals/uvs
    for (const auto& obs : obstacles){
        glm::vec3 cb = glm::vec3(
            (obs.aabb.min.x + obs.aabb.max.x) * 0.5f,
             obs.aabb.min.y,
            (obs.aabb.min.z + obs.aabb.max.z) * 0.5f);
        glm::mat4 m = glm::translate(glm::mat4(1.0f), cb);
        glUniformMatrix4fv(glGetUniformLocation(scene.shadow_shader.id, "u_model"),
            1, GL_FALSE, glm::value_ptr(m));
    }

    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT);
}

void scene_draw_sky(SceneState& scene, const glm::mat4& view, const glm::mat4& proj){
    if (!scene.sky_tex) return;

    glm::mat4 rot_only = glm::mat4(glm::mat3(view));
    glm::mat4 inv_vp = glm::inverse(proj * rot_only);

    shader_bind(scene.sky_shader);
    glUniformMatrix4fv(glGetUniformLocation(scene.sky_shader.id, "u_inv_view_proj"),
        1, GL_FALSE, glm::value_ptr(inv_vp));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene.sky_tex);
    glUniform1i(glGetUniformLocation(scene.sky_shader.id, "u_sky_tex"), 0);

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
    const glm::mat4& view,
    const glm::mat4& proj,
    bool show_hitboxes)
{
    // ground
    glm::mat4 gm = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, Const::GROUND_Y_OFFSET, 0.0f));
    glm::mat3 gnm = glm::mat3(1.0f);

    shader_bind(scene.shader);
    set_mat4(scene.shader, "u_view", view);
    set_mat4(scene.shader, "u_proj", proj);
    set_vec3(scene.shader, "u_light_dir", LIGHT_DIR);
    set_mat4(scene.shader, "u_model", gm);
    set_mat3(scene.shader, "u_normal_mat", gnm);
    set_vec3(scene.shader, "u_kd", glm::vec3(Const::GROUND_KD));
    glUniform3f(glGetUniformLocation(scene.shader.id, "u_kd_alt"),
        Const::GROUND_KD_ALT, Const::GROUND_KD_ALT, Const::GROUND_KD_ALT);
    glUniform1f(glGetUniformLocation(scene.shader.id, "u_checker_scale"),
        1.0f / Const::GROUND_GRID_TILE_SIZE);
    glUniform1i(glGetUniformLocation(scene.shader.id, "u_use_checker"), 1);

    // remove as comment line to enable checker grids
    //glBindVertexArray(scene.ground.vao);
    //glDrawArrays(GL_TRIANGLES, 0, scene.ground.count);
    //glBindVertexArray(0);
    glUniform1i(glGetUniformLocation(scene.shader.id, "u_use_checker"), 0);

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

    glm::mat3 tnm = glm::mat3(glm::transpose(glm::inverse(tm)));

    shader_bind(scene.shader);
    set_mat4(scene.shader, "u_model", tm);
    set_mat3(scene.shader, "u_normal_mat", tnm);

    if constexpr (Const::USE_PROC_MESH){
        // proc mesh uses rgb color layout not normals 
        // for now I'll draw with gizmo shader
        // lighting won't apply but colors are baked per face in mesh_builder
        shader_bind(scene.gizmo_shader);
        set_mat4(scene.gizmo_shader, "u_view", view);
        set_mat4(scene.gizmo_shader, "u_proj", proj);
        set_mat4(scene.gizmo_shader, "u_model", tm);
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
        std::vector<float> wire_verts;
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
                wire_verts.insert(wire_verts.end(),
                    {p.x,p.y,p.z, col.r,col.g,col.b});
            }
        };

        push_aabb(trike.aabb, {0.0f,1.0f,0.3f});
        for (const auto& obs : obstacles)
            push_aabb(obs.aabb, {1.0f,0.9f,0.0f});

        Mesh wire;
        mesh_init(wire, wire_verts);
        shader_bind(scene.gizmo_shader);
        set_mat4(scene.gizmo_shader, "u_view", view);
        set_mat4(scene.gizmo_shader, "u_proj", proj);
        set_mat4(scene.gizmo_shader, "u_model", glm::mat4(1.0f));
        glBindVertexArray(wire.vao);
        glDrawArrays(GL_LINES, 0, wire.count);
        glBindVertexArray(0);
        mesh_destroy(wire);
    }

    shader_bind(scene.shader);
}

void scene_destroy(SceneState& scene){
    if (scene.sky_tex) glDeleteTextures(1, &scene.sky_tex);
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