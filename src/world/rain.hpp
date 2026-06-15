#pragma once
#include "../renderer/shader.hpp"
#include "../renderer/mesh.hpp"
#include "../core/const.hpp"
#include "../world/height_field.hpp"
#include <glm/glm.hpp>
#include <string>

struct RainParticle {
    glm::vec3 pos;
    float life;
    float len_scale;
    float width_scale;
    float alpha_scale;
};

struct RainSplash {
    glm::vec3 pos;
    float life;
    float radius;
    bool alive = false;
};

struct RainState{
    bool active = false;
    float intensity = 1.0f;
    float timer = 0.0f;

    float thunder_timer = 3.0f; // countdown to next trigger
    float flash_alpha = 0.0f; // current fullscreen flash opacity
    float flash_decay = 3.5f; // active decay rate
    float thunder_audio_delay = 0.0f; // countdown to play boom  after flash
    bool thunder_boom_pending = false;
    float preflash_gap_timer = 0.0f; // countdown to main strike after pre-flash
    bool main_strike_pending = false; // true while waiting to fire main after pre

    RainParticle particles[Const::RAIN_PARTICLE_COUNT];

    RainSplash splashes[Const::RAIN_SPLASH_MAX];
    int splash_head = 0; // ring buffer write head

    Shader shader;
    Mesh mesh;

    GLuint splash_vao = 0;
    GLuint splash_vbo = 0;
    Shader splash_shader;

    // fullscreen lighting flash
    GLuint flash_vao = 0;
    GLuint flash_vbo = 0;
    Shader flash_shader;
    GLint flash_loc_alpha = -1;
    GLint flash_loc_res = -1;

    struct {
        GLint view, proj;
        GLint cam_right, streak_dir;
        GLint streak_len, streak_width;
        GLint alpha;
        GLint cam_center, box_half_xz;
    } loc;

    struct {
        GLint view, proj;
        GLint alpha;
    } splash_loc;
};

void rain_init(RainState& rain, glm::vec3 cam_pos);
void rain_update(RainState& rain, float dt, glm::vec3 cam_pos, float speed, float heading, const HeightField& terrain);
void rain_draw(RainState& rain, const glm::mat4& view, const glm::mat4& proj, glm::vec3 cam_pos, 
 float speed, float heading);
void rain_destroy(RainState& rain);
void rain_tick_trigger(RainState& rain, float dt);
void rain_tick_thunder(RainState& rain, float dt, const std::string& assets_root);