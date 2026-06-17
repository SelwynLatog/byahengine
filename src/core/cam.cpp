#include "cam.hpp"
#include "const.hpp"
#include "../physics/trike_state.hpp"
#include "../physics/player_state.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

void cam_seed(CamState& cam, const App& app){
    cam.yaw = 0.0f;
    float cam_world_angle = glm::radians(cam.yaw) + glm::radians(180.0f);
    float pitch_r    = glm::radians(cam.pitch);
    float foot_dist  = 4.0f;
    glm::vec3 origin = app.player.pos + glm::vec3(0.0f, 1.0f, 0.0f);
    cam.pos = origin + glm::vec3(
        foot_dist * cosf(pitch_r) * cosf(cam_world_angle),
        foot_dist * sinf(pitch_r),
        foot_dist * cosf(pitch_r) * sinf(cam_world_angle));
}

glm::mat4 cam_update(CamState& cam, const App& app, float dt, bool arrow_held){
    if (app.player.mode == PLAYER_FOOT){
        float cam_world_yaw = glm::radians(cam.yaw);
        float pitch_r = glm::radians(cam.pitch);
        pitch_r = glm::clamp(pitch_r,
            glm::radians(Const::CAM_PITCH_MIN),
            glm::radians(Const::CAM_PITCH_MAX));

        float foot_dist  = 4.0f;
        glm::vec3 origin = app.player.pos + glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 ideal_eye = origin + glm::vec3(
            foot_dist * cosf(pitch_r) * cosf(cam_world_yaw),
            foot_dist * sinf(pitch_r),
            foot_dist * cosf(pitch_r) * sinf(cam_world_yaw));

        // snap on first frame after dismount, then smooth lerpr
        if (cam.needs_snap){
            cam.pos = ideal_eye;
            cam.needs_snap = false;
        }
        else {
            // remove 0.25 for a snappier lerp
            // TODO: toggleable
            cam.pos = glm::mix(cam.pos, ideal_eye, Const::CAM_LERP_SPEED * 0.25f * dt);
        }
        return glm::lookAt(cam.pos, origin, glm::vec3(0,1,0));
    }

    // trike mode
    float yaw_r = glm::radians(cam.yaw);
    float cam_yaw_world = app.trike.heading + yaw_r + glm::radians(180.0f);

    float speed_t = glm::clamp(std::abs(app.trike.speed) / Const::TRIKE_MAX_SPEED, 0.0f, 1.0f);
    float slope_contribution = -app.trike.pitch_angle * speed_t * Const::CAM_SLOPE_PITCH_SCALE;

    static float s_pitch_smoothed = 0.0f;
    s_pitch_smoothed = glm::mix(s_pitch_smoothed, slope_contribution,
        glm::clamp(Const::CAM_SLOPE_LERP_SPEED * dt, 0.0f, 1.0f));

    float pitch_r = glm::radians(cam.pitch) + s_pitch_smoothed;
    pitch_r = glm::clamp(pitch_r,
        glm::radians(Const::CAM_PITCH_MIN),
        glm::radians(Const::CAM_PITCH_MAX + 25.0f));

    static float s_target_y_bias = 0.0f;
    float target_y_goal = -app.trike.pitch_angle * speed_t * Const::CAM_SLOPE_TARGET_Y_BIAS;
    s_target_y_bias = glm::mix(s_target_y_bias, target_y_goal,
        glm::clamp(Const::CAM_SLOPE_LERP_SPEED * dt, 0.0f, 1.0f));

    glm::vec3 origin   = app.trike.position + glm::vec3(0.0f, Const::CAM_ORBIT_TARGET_Y, 0.0f);
    glm::vec3 ideal_eye = origin + glm::vec3(
        cam.dist * cosf(pitch_r) * cosf(cam_yaw_world),
        cam.dist * sinf(pitch_r),
        cam.dist * cosf(pitch_r) * sinf(cam_yaw_world));
    cam.pos = glm::mix(cam.pos, ideal_eye, Const::CAM_LERP_SPEED * dt);

    float fwd_angle = app.trike.heading;
    glm::vec3 fwd   = glm::vec3(cosf(fwd_angle), 0.0f, sinf(fwd_angle));
    float lookahead  = (app.trike.speed / Const::TRIKE_MAX_SPEED) * Const::CAM_LOOKAHEAD;
    glm::vec3 target = origin + fwd * lookahead + glm::vec3(0.0f, s_target_y_bias, 0.0f);

    // camera shake on impact
    if (app.trike.impact_timer > 0.0f){
        float t = app.trike.impact_timer;
        float mag = glm::clamp(app.trike.last_impact_force * 0.018f, 0.0f, 0.4f);
        float decay = t / 0.35f;
        target.x += std::sin(t * 47.0f) * mag * decay;
        target.y += std::cos(t * 31.0f) * mag * decay;
        target.z += std::sin(t * 23.0f) * mag * decay;
    }

    if (cam.free_cam){
        glm::vec3 top = app.trike.position + glm::vec3(0.0f, 15.0f, 0.0f);
        return glm::lookAt(top, app.trike.position, glm::vec3(1,0,0));
    }
    return glm::lookAt(cam.pos, target, glm::vec3(0,1,0));
}