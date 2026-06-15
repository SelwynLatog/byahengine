#include "rain.hpp"
#include "../core/const.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <cstdlib>
#include <cmath>


/*
 * RAIN SYSTEM 
 * billboard quad particles n pos update every frame
 * vertex shader expands each pos into a cam facing quad
 * angled by wind dir to get that streak look
 */
static const char* RAIN_VERT = R"(
#version 330 core
    
layout(location = 0) in vec3  a_pos;
layout(location = 1) in float a_len_scale;
layout(location = 2) in float a_width_scale;
layout(location = 3) in float a_alpha_scale;

uniform mat4  u_view;
uniform mat4  u_proj;
uniform vec3  u_cam_right;
uniform vec3  u_streak_dir;
uniform float u_streak_len;
uniform float u_streak_width;
uniform vec3  u_cam_center;
uniform float u_box_half_xz;
    
out float v_alpha;
out float v_edge_fade;

void main(){
    int qi = gl_VertexID % 6;
    
    // fade out particles in outer 35% of box so hard edge is never visible
    float dx = abs(a_pos.x - u_cam_center.x) / u_box_half_xz;
    float dz = abs(a_pos.z - u_cam_center.z) / u_box_half_xz;
    float edge = max(dx, dz);
    v_edge_fade = 1.0 - smoothstep(0.65, 1.0, edge);

    vec3 top = a_pos;
    vec3 bot = a_pos + u_streak_dir * (u_streak_len * a_len_scale);
    vec3 right = u_cam_right * (u_streak_width * a_width_scale);
    v_alpha = a_alpha_scale;
    
    vec3 TL = top - right;
    vec3 TR = top + right;
    vec3 BL = bot - right;
    vec3 BR = bot + right;
    
    vec3 p;
    if (qi == 0) p = TL;
    else if (qi == 1) p = BL;
    else if (qi == 2) p = TR;
    else if (qi == 3) p = BL;
    else if (qi == 4) p = BR;
    else p = TR;
    
     gl_Position = u_proj * u_view * vec4(p, 1.0);
}
)";


static const char* RAIN_FRAG = R"(
#version 330 core
in float v_alpha;
in float v_edge_fade;
out vec4 frag_color;
uniform float u_alpha;
void main(){
    // rain frag color edit here
    frag_color = vec4(0.78, 0.88, 1.00, u_alpha * v_alpha * v_edge_fade);
}
)";

static const char* SPLASH_VERT = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_view;
uniform mat4 u_proj;
void main(){
    gl_Position = u_proj * u_view * vec4(a_pos, 1.0);
}
)";

static const char* SPLASH_FRAG = R"(
#version 330 core
out vec4 frag_color;
uniform float u_alpha;
void main(){
    frag_color = vec4(0.82, 0.92, 1.00, u_alpha);
}
)";

static const char* FLASH_VERT = R"(
#version 330 core
const vec2 VERTS[6] = vec2[](
    vec2(-1,-1), vec2(1,-1), vec2(1,1),
    vec2(-1,-1), vec2(1,1), vec2(-1,1)
);
void main(){
    gl_Position = vec4(VERTS[gl_VertexID], 0.0, 1.0);
}
)";

static const char* FLASH_FRAG = R"(
#version 330 core
out vec4 frag_color;
uniform float u_alpha;
uniform vec2  u_res;
void main(){
    vec2 uv = (gl_FragCoord.xy / u_res) * 2.0 - 1.0;
    float vignette = 1.0 - smoothstep(0.3, 1.4, length(uv));
    frag_color = vec4(0.90, 0.95, 1.0, u_alpha * (0.6 + 0.4 * vignette));
}
)";

static float randf(float lo, float hi){
    return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

// scatter one particle randomly inside the spawn box centered on cam
static void spawn_particle(RainParticle& p, glm::vec3 cam_pos){
    p.pos.x = cam_pos.x + randf(-Const::RAIN_BOX_HALF_XZ, Const::RAIN_BOX_HALF_XZ);
    p.pos.z = cam_pos.z + randf(-Const::RAIN_BOX_HALF_XZ, Const::RAIN_BOX_HALF_XZ);
    p.pos.y = cam_pos.y + randf(0.0f, Const::RAIN_BOX_HEIGHT);
    p.life = randf(0.0f, 1.0f);
    p.len_scale = randf(0.6f, 1.4f);
    p.width_scale = randf(0.7f, 1.3f);
    p.alpha_scale = randf(0.4f, 1.0f);
}

void rain_init(RainState& rain, glm::vec3 cam_pos){
    shader_init(rain.shader, RAIN_VERT, RAIN_FRAG);

    GLuint id = rain.shader.id;
    rain.loc.view = glGetUniformLocation(id, "u_view");
    rain.loc.proj = glGetUniformLocation(id, "u_proj");
    rain.loc.cam_right = glGetUniformLocation(id, "u_cam_right");
    rain.loc.streak_dir = glGetUniformLocation(id, "u_streak_dir");
    rain.loc.streak_len = glGetUniformLocation(id, "u_streak_len");
    rain.loc.streak_width = glGetUniformLocation(id, "u_streak_width");
    rain.loc.alpha = glGetUniformLocation(id, "u_alpha");
    rain.loc.cam_center = glGetUniformLocation(id, "u_cam_center");
    rain.loc.box_half_xz = glGetUniformLocation(id, "u_box_half_xz");

    // scatter initial particles
    for (int i = 0; i < Const::RAIN_PARTICLE_COUNT; i++)
        spawn_particle(rain.particles[i], cam_pos);

    // N particles * 6 verts each
    // all 6 verts of a particle share the same a_pos
    // shader derives the 4 corners from gl_VertexID
    // VBO layout: vec3 pos | float len_s | float width_s | float alpha_s
    // stride = 6 floats = 24 bytes
    int total_verts = Const::RAIN_PARTICLE_COUNT * 6;
    std::vector<float> init_data(total_verts * 6, 0.0f);

    glGenVertexArrays(1, &rain.mesh.vao);
    glGenBuffers(1, &rain.mesh.vbo);
    glBindVertexArray(rain.mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, rain.mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, init_data.size() * sizeof(float), init_data.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);

    rain.mesh.count = total_verts;

    rain.active = Const::RAIN_FORCE_ENABLE;
    // seed random timer for first event when not forced
    rain.timer = randf(Const::RAIN_INTERVAL_MIN, Const::RAIN_INTERVAL_MAX);

    // splash ring pool GL setup
    shader_init(rain.splash_shader, SPLASH_VERT, SPLASH_FRAG);
    rain.splash_loc.view = glGetUniformLocation(rain.splash_shader.id, "u_view");
    rain.splash_loc.proj = glGetUniformLocation(rain.splash_shader.id, "u_proj");
    rain.splash_loc.alpha = glGetUniformLocation(rain.splash_shader.id, "u_alpha");

    glGenVertexArrays(1, &rain.splash_vao);
    glGenBuffers(1, &rain.splash_vbo);
    glBindVertexArray(rain.splash_vao);
    glBindBuffer(GL_ARRAY_BUFFER, rain.splash_vbo);
    glBufferData(GL_ARRAY_BUFFER, Const::RAIN_SPLASH_MAX * 8 * 2 * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);


    // flash quad
    shader_init(rain.flash_shader, FLASH_VERT, FLASH_FRAG);
    rain.flash_loc_alpha = glGetUniformLocation(rain.flash_shader.id, "u_alpha");
    rain.flash_loc_res = glGetUniformLocation(rain.flash_shader.id, "u_res");
    glGenVertexArrays(1, &rain.flash_vao);
    rain.thunder_timer = randf(Const::THUNDER_INTERVAL_MIN, Const::THUNDER_INTERVAL_MAX);
}

void rain_update(RainState& rain, float dt, glm::vec3 cam_pos, float speed, float heading, const HeightField& terrain){
    if (!rain.active || Const::RAIN_PAUSE) return;

    float box_floor = cam_pos.y - 2.0f;

    glm::vec3 fall = { Const::RAIN_WIND_X, -Const::RAIN_FALL_SPEED, Const::RAIN_WIND_Z };
    glm::vec3 vel = fall * dt;

    for (int i = 0; i < Const::RAIN_PARTICLE_COUNT; i++){
        rain.particles[i].pos += vel;

        float dx = rain.particles[i].pos.x - cam_pos.x;
        float dz = rain.particles[i].pos.z - cam_pos.z;
        bool out_xz = (std::abs(dx) > Const::RAIN_BOX_HALF_XZ ||
                       std::abs(dz) > Const::RAIN_BOX_HALF_XZ);
        bool out_y  = (rain.particles[i].pos.y < box_floor);

        if (out_y){
            RainSplash& sp = rain.splashes[rain.splash_head];
            rain.splash_head = (rain.splash_head + 1) % Const::RAIN_SPLASH_MAX;
            sp.pos = rain.particles[i].pos;
            sp.pos.y = heightfield_sample(terrain, sp.pos.x, sp.pos.z) + 0.05f;
            sp.life = 0.0f;
            sp.radius = 0.0f;
            sp.alive = true;
        }

        if (out_y || out_xz)
            spawn_particle(rain.particles[i], cam_pos);
    }

    // tick splashes
    float life_step = dt / Const::RAIN_SPLASH_LIFE;
    for (int i = 0; i < Const::RAIN_SPLASH_MAX; i++){
        if (!rain.splashes[i].alive) continue;
        if (rain.splashes[i].life >= 1.0f){ rain.splashes[i].alive = false; continue; }
        rain.splashes[i].life += life_step;
        rain.splashes[i].radius = rain.splashes[i].life * Const::RAIN_SPLASH_RADIUS;
    }
}

void rain_draw(RainState& rain, const glm::mat4& view, const glm::mat4& proj, glm::vec3 cam_pos, float speed, float heading){
    if (!rain.active) return;

    // pack VBO: 6 floats per vert (pos xyz, len_s, width_s, alpha_s)
    static float vbuf[Const::RAIN_PARTICLE_COUNT * 6 * 6];
    int idx = 0;
    for (int i = 0; i < Const::RAIN_PARTICLE_COUNT; i++){
        float x = rain.particles[i].pos.x;
        float y = rain.particles[i].pos.y;
        float z = rain.particles[i].pos.z;
        float ls = rain.particles[i].len_scale;
        float ws = rain.particles[i].width_scale;
        float as = rain.particles[i].alpha_scale;
        for (int v = 0; v < 6; v++){
            vbuf[idx++] = x;
            vbuf[idx++] = y;
            vbuf[idx++] = z;
            vbuf[idx++] = ls;
            vbuf[idx++] = ws;
            vbuf[idx++] = as;
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, rain.mesh.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vbuf), vbuf);

    glm::vec3 cam_right = glm::vec3(view[0][0], view[1][0], view[2][0]);

    // motion lean
    // blend trike heading into wind so streaks tilt toward the player
    // speed_factor capped so streaks never flatten past 30 degrees from vert
    glm::vec3 wind_down = glm::vec3(Const::RAIN_WIND_X, -Const::RAIN_FALL_SPEED, Const::RAIN_WIND_Z);
    glm::vec3 cam_fwd = -glm::vec3(view[0][2], view[1][2], view[2][2]);
    cam_fwd.y = 0.0f;
    if (glm::length(cam_fwd) > 0.001f) cam_fwd = glm::normalize(cam_fwd);
    float speed_factor = glm::clamp(speed * 0.06f, -0.8f, 0.8f);
    glm::vec3 motion = -cam_fwd * speed_factor;
    glm::vec3 streak_dir = glm::normalize(wind_down + motion * Const::RAIN_FALL_SPEED);

    shader_bind(rain.shader);
    glUniformMatrix4fv(rain.loc.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(rain.loc.proj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform3f(rain.loc.cam_right, cam_right.x,  cam_right.y,  cam_right.z);
    glUniform3f(rain.loc.streak_dir, streak_dir.x, streak_dir.y, streak_dir.z);
    glUniform1f(rain.loc.streak_len, Const::RAIN_STREAK_LENGTH);
    glUniform1f(rain.loc.streak_width, Const::RAIN_STREAK_WIDTH);
    glUniform1f(rain.loc.alpha, Const::RAIN_ALPHA);
    glUniform3f(rain.loc.cam_center, cam_pos.x, cam_pos.y, cam_pos.z);
    glUniform1f(rain.loc.box_half_xz, Const::RAIN_BOX_HALF_XZ);

    // additive blend: streaks brighten without harsh cutouts
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(rain.mesh.vao);
    glDrawArrays(GL_TRIANGLES, 0, rain.mesh.count);
    glBindVertexArray(0);

    // SPLASH DRAW
    // build line ring verts for all live splashes
    // 8 segments per ring = 16 verts per splash
    static float sbuf[Const::RAIN_SPLASH_MAX * 8 * 2 * 3];
    int scount = 0;
    int si = 0;
    for (int i = 0; i < Const::RAIN_SPLASH_MAX; i++){
        const RainSplash& sp = rain.splashes[i];
        if (!sp.alive) continue;
        float r = sp.radius;
        for (int seg = 0; seg < 8; seg++){
            float a0 = (seg) * (glm::two_pi<float>() / 8.0f);
            float a1 = (seg + 1) * (glm::two_pi<float>() / 8.0f);
            sbuf[si++] = sp.pos.x + std::cos(a0) * r;
            sbuf[si++] = sp.pos.y + 0.02f;
            sbuf[si++] = sp.pos.z + std::sin(a0) * r;
            sbuf[si++] = sp.pos.x + std::cos(a1) * r;
            sbuf[si++] = sp.pos.y + 0.02f;
            sbuf[si++] = sp.pos.z + std::sin(a1) * r;
            scount += 2;
        }
    }

    if (scount > 0){
        glBindBuffer(GL_ARRAY_BUFFER, rain.splash_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, si * sizeof(float), sbuf);

        shader_bind(rain.splash_shader);
        glUniformMatrix4fv(rain.splash_loc.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(rain.splash_loc.proj, 1, GL_FALSE, glm::value_ptr(proj));
        glUniform1f(rain.splash_loc.alpha, Const::RAIN_ALPHA * 2.5f);

        glBindVertexArray(rain.splash_vao);
        glDrawArrays(GL_LINES, 0, scount);
        glBindVertexArray(0);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // fullscreen lightning flash overlay
    if (Const::RAIN_THUNDERSTORM && rain.flash_alpha > 0.001f){
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        shader_bind(rain.flash_shader);
        glUniform1f(rain.flash_loc_alpha, rain.flash_alpha);
        glUniform2f(rain.flash_loc_res, (float)Const::WINDOW_WIDTH, (float)Const::WINDOW_HEIGHT);
        glBindVertexArray(rain.flash_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
    }

    glDisable(GL_BLEND);
}

// random on/off when RAIN_FORCE_ENABLE is false
void rain_tick_trigger(RainState& rain, float dt){
    if (Const::RAIN_FORCE_ENABLE) return; // constant overrides, nothing to do

    rain.timer -= dt;
    if (rain.timer > 0.0f) return;

    if (!rain.active){

        // roll for rain start
        if (randf(0.0f, 1.0f) < Const::RAIN_START_CHANCE){
            rain.active = true;
            rain.timer = Const::RAIN_DUR;
        }
        else{
            rain.timer = randf(
                Const::RAIN_INTERVAL_MIN,
                Const::RAIN_INTERVAL_MAX
            );
        }
    }
    else {
        // end ,schedule next
        rain.active = false;
        rain.timer = randf(Const::RAIN_INTERVAL_MIN, Const::RAIN_INTERVAL_MAX);
    }
}

void rain_tick_thunder(RainState& rain, float dt, const std::string& assets_root){
    if (!rain.active || !Const::RAIN_THUNDERSTORM) return;

    // decay active flash
    if (rain.flash_alpha > 0.0f){
        rain.flash_alpha -= rain.flash_decay * dt;
        if (rain.flash_alpha < 0.0f) rain.flash_alpha = 0.0f;
    }

    // delayed audio boom
    if (rain.thunder_boom_pending){
        rain.thunder_audio_delay -= dt;
        // app.cpp reads boom_pending + delay <= 0 to fire audio
    }

    // waiting to fire main strike after pre-flash gap
    if (rain.main_strike_pending){
        rain.preflash_gap_timer -= dt;
        if (rain.preflash_gap_timer <= 0.0f){
            rain.main_strike_pending = false;
            rain.flash_alpha = Const::THUNDER_FLASH_ALPHA_MAX;
            rain.flash_decay = Const::THUNDER_FLASH_DECAY;
            rain.thunder_audio_delay = randf(Const::THUNDER_AUDIO_DELAY_MIN, Const::THUNDER_AUDIO_DELAY_MAX);
            rain.thunder_boom_pending = true;
        }
        return;
    }

    rain.thunder_timer -= dt;
    if (rain.thunder_timer > 0.0f) return;

    // new strike coin fli[]
    if (randf(0.0f, 1.0f) < Const::THUNDER_DOUBLE_CHANCE){
        // pre-flash dim, fast decay, then gap before main
        rain.flash_alpha = Const::THUNDER_PREFLASH_ALPHA;
        rain.flash_decay = Const::THUNDER_PREFLASH_DECAY;
        rain.preflash_gap_timer = Const::THUNDER_PREFLASH_GAP;
        rain.main_strike_pending = true;
    }
    else {
        // single strike directly
        rain.flash_alpha = Const::THUNDER_FLASH_ALPHA_MAX;
        rain.flash_decay = Const::THUNDER_FLASH_DECAY;
        rain.thunder_audio_delay = randf(Const::THUNDER_AUDIO_DELAY_MIN, Const::THUNDER_AUDIO_DELAY_MAX);
        rain.thunder_boom_pending = true;
    }
    rain.thunder_timer = randf(Const::THUNDER_INTERVAL_MIN, Const::THUNDER_INTERVAL_MAX);
}

void rain_destroy(RainState& rain){
    shader_destroy(rain.shader);
    shader_destroy(rain.splash_shader);
    if (rain.mesh.vao) glDeleteVertexArrays(1, &rain.mesh.vao);
    if (rain.mesh.vbo) glDeleteBuffers(1, &rain.mesh.vbo);
    if (rain.splash_vao) glDeleteVertexArrays(1, &rain.splash_vao);
    if (rain.splash_vbo) glDeleteBuffers(1, &rain.splash_vbo);
    rain.mesh.vao = rain.mesh.vbo = 0;
    rain.splash_vao = rain.splash_vbo = 0;
    if (rain.flash_vao) glDeleteVertexArrays(1, &rain.flash_vao);
    rain.flash_vao = 0;
}