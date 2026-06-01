#include "driver_anim.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

// rotate a matrix around a pivot point in model space
// equiv to: translate(pivot) * rotate * translate(-pivot)
static glm::mat4 rot_around(glm::vec3 pivot, float angle, glm::vec3 axis) {
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, pivot);
    m = glm::rotate(m, angle, axis);
    m = glm::translate(m, -pivot);
    return m;
}

static void pose_walk(DriverPose& pose, float t, float speed) {
    // amplitude scales with speed, clamped so slow walk looks natural
    float amp = glm::clamp(speed / 4.0f, 0.15f, 0.55f);
    float swing = std::sin(t) * amp;

    // legs swing forward/back around hip pivot (Y=0 in bone local, pivot computed externally)
    // pivot is passed as zero here — driver_model_draw offsets by the actual pivot
    pose.local[BONE_LEG_L]  = glm::rotate(glm::mat4(1.0f),  swing, glm::vec3(1,0,0));
    pose.local[BONE_LEG_R]  = glm::rotate(glm::mat4(1.0f), -swing, glm::vec3(1,0,0));

    // arms swing opposite to legs
    float arm_amp = amp * 0.6f;
    pose.local[BONE_ARM_L]  = glm::rotate(glm::mat4(1.0f), -swing * arm_amp / amp, glm::vec3(1,0,0));
    pose.local[BONE_ARM_R]  = glm::rotate(glm::mat4(1.0f),  swing * arm_amp / amp, glm::vec3(1,0,0));

    // head bobs very subtly
    float bob_angle = std::sin(t * 2.0f) * 0.015f;
    pose.local[BONE_HEAD]   = glm::rotate(glm::mat4(1.0f), bob_angle, glm::vec3(1,0,0));

    pose.local[BONE_TORSO]  = glm::mat4(1.0f);
}

static void pose_sit(DriverPose& pose) {
    // legs bent forward at roughly 80 degrees (sitting on seat)
    float leg_bend = glm::radians(80.0f);
    pose.local[BONE_LEG_L]  = glm::rotate(glm::mat4(1.0f),  leg_bend, glm::vec3(1,0,0));
    pose.local[BONE_LEG_R]  = glm::rotate(glm::mat4(1.0f),  leg_bend, glm::vec3(1,0,0));

    // arms forward and slightly down, gripping handlebars
    float arm_fwd  = glm::radians(-55.0f);
    float arm_down = glm::radians(15.0f);
    glm::mat4 arm_base = glm::rotate(glm::mat4(1.0f), arm_fwd,  glm::vec3(1,0,0));
    arm_base            = glm::rotate(arm_base,         arm_down, glm::vec3(0,0,1));
    pose.local[BONE_ARM_L]  = arm_base;
    arm_base = glm::rotate(glm::mat4(1.0f), arm_fwd,  glm::vec3(1,0,0));
    arm_base = glm::rotate(arm_base,        -arm_down, glm::vec3(0,0,1));
    pose.local[BONE_ARM_R]  = arm_base;

    // slight forward lean on torso
    pose.local[BONE_TORSO]  = glm::rotate(glm::mat4(1.0f), glm::radians(12.0f), glm::vec3(1,0,0));
    pose.local[BONE_HEAD]   = glm::mat4(1.0f);
}

void driver_pose_compute(DriverPose& pose, float anim_timer,
                         float speed, int mode, float mount_t) {
    // init all to identity
    for (int i = 0; i < BONE_COUNT; i++)
        pose.local[i] = glm::mat4(1.0f);

    if (mode == 0) {
        pose_walk(pose, anim_timer, speed);
        return;
    }

    if (mode == 1) {
        pose_sit(pose);
        return;
    }

    // mode == 2: lerp walk -> sit by mount_t (0 = walk, 1 = sit)
    DriverPose walk_pose, sit_pose;
    for (int i = 0; i < BONE_COUNT; i++) {
        walk_pose.local[i] = glm::mat4(1.0f);
        sit_pose.local[i]  = glm::mat4(1.0f);
    }
    pose_walk(walk_pose, anim_timer, speed);
    pose_sit(sit_pose);

    // per-element lerp on the mat4 columns
    float t = glm::clamp(mount_t, 0.0f, 1.0f);
    for (int i = 0; i < BONE_COUNT; i++)
        for (int c = 0; c < 4; c++)
            pose.local[i][c] = glm::mix(walk_pose.local[i][c], sit_pose.local[i][c], t);
}