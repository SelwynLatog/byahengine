#pragma once
#include "../renderer/shader.hpp"
#include "../renderer/mesh.hpp"
#include "../core/const.hpp"
#include "../world/height_field.hpp"
#include <glm/glm.hpp>

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
};

struct RainState{
    bool active = false;
    float intensity = 1.0f;
    float timer = 0.0f;

    RainParticle particles[Const::RAIN_PARTICLE_COUNT];

    RainSplash splashes[Const::RAIN_SPLASH_MAX];
    int splash_head = 0; // ring buffer write head

    Shader shader;
    Mesh mesh;

    GLuint splash_vao = 0;
    GLuint splash_vbo = 0;
    Shader splash_shader;

    struct {
        GLint view, proj;
        GLint cam_right, streak_dir;
        GLint streak_len, streak_width;
        GLint alpha;
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