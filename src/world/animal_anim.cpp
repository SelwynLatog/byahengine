#include "animal_anim.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

// helper: rotate a bone local matrix around an axis by angle in radians
static glm::mat4 rot(glm::mat4 m, float angle, glm::vec3 axis){
    return glm::rotate(m, angle, axis);
}

// stride: alternating leg swing, returns angle for one leg given phase offset
// t = anim_timer, freq = cycles per second, amp = max swing angle radians
static float stride(float t, float freq, float amp, float phase){
    return std::sin(t * freq * 6.2831853f + phase) * amp;
}

void animal_anim_chicken(DriverPose& pose, float anim_timer, float speed, bool fleeing){
    // identity baseline 
    // all bones rest
    for (int i = 0; i < BONE_COUNT; i++)
        pose.local[i] = glm::mat4(1.0f);

    float freq  = fleeing ? 3.5f : 1.8f;
    float amp   = fleeing ? 0.5f : 0.25f;

    // legs stride alternating
    pose.local[BONE_LEG_L] = rot(pose.local[BONE_LEG_L],
        stride(anim_timer, freq, amp, 0.0f), glm::vec3(1,0,0));
    pose.local[BONE_LEG_R] = rot(pose.local[BONE_LEG_R],
        stride(anim_timer, freq, amp, 3.14159f), glm::vec3(1,0,0));

    // head bobs forward on each step
    float head_bob = std::abs(std::sin(anim_timer * freq * 6.2831853f)) * 0.15f;
    pose.local[BONE_HEAD] = rot(pose.local[BONE_HEAD],
        head_bob, glm::vec3(1,0,0));

    // wings: slight flap when fleeing, tucked when walking
    float wing_amp = fleeing ? 0.6f : 0.05f;
    float wing_flap = std::sin(anim_timer * (fleeing ? 8.0f : 1.0f) * 6.2831853f) * wing_amp;
    pose.local[BONE_ARM_L] = rot(pose.local[BONE_ARM_L],  wing_flap, glm::vec3(0,0,1));
    pose.local[BONE_ARM_R] = rot(pose.local[BONE_ARM_R], -wing_flap, glm::vec3(0,0,1));
}

void animal_anim_cow(DriverPose& pose, float anim_timer, float speed, bool fleeing, bool grazing){
    for (int i = 0; i < BONE_COUNT; i++)
        pose.local[i] = glm::mat4(1.0f);

    if (grazing){
        // head dips down slowly while grazing
        float graze_dip = 0.4f + std::sin(anim_timer * 0.5f * 6.2831853f) * 0.1f;
        pose.local[BONE_HEAD] = rot(pose.local[BONE_HEAD], graze_dip, glm::vec3(1,0,0));
        return;
    }

    float freq = fleeing ? 1.8f : 0.8f;
    float amp  = fleeing ? 0.3f : 0.18f;

    // diagonal pairs: FL+RR together, FR+RL together (trot gait)
    pose.local[BONE_LEG_L] = rot(pose.local[BONE_LEG_L],
        stride(anim_timer, freq, amp, 0.0f), glm::vec3(1,0,0));
    pose.local[BONE_LEG_R] = rot(pose.local[BONE_LEG_R],
        stride(anim_timer, freq, amp, 3.14159f), glm::vec3(1,0,0));
    pose.local[BONE_ARM_L] = rot(pose.local[BONE_ARM_L],
        stride(anim_timer, freq, amp, 3.14159f), glm::vec3(1,0,0));
    pose.local[BONE_ARM_R] = rot(pose.local[BONE_ARM_R],
        stride(anim_timer, freq, amp, 0.0f), glm::vec3(1,0,0));

    // slight head nod on each stride
    pose.local[BONE_HEAD] = rot(pose.local[BONE_HEAD],
        std::abs(stride(anim_timer, freq, 0.08f, 0.0f)), glm::vec3(1,0,0));
}

void animal_anim_cat(DriverPose& pose, float anim_timer, float speed, bool fleeing){
    for (int i = 0; i < BONE_COUNT; i++)
        pose.local[i] = glm::mat4(1.0f);

    float freq = fleeing ? 3.0f : 1.4f;
    float amp  = fleeing ? 0.45f : 0.22f;

    // diagonal trot same as cow
    pose.local[BONE_LEG_L] = rot(pose.local[BONE_LEG_L],
        stride(anim_timer, freq, amp, 0.0f), glm::vec3(1,0,0));
    pose.local[BONE_LEG_R] = rot(pose.local[BONE_LEG_R],
        stride(anim_timer, freq, amp, 3.14159f), glm::vec3(1,0,0));
    pose.local[BONE_ARM_L] = rot(pose.local[BONE_ARM_L],
        stride(anim_timer, freq, amp, 3.14159f), glm::vec3(1,0,0));
    pose.local[BONE_ARM_R] = rot(pose.local[BONE_ARM_R],
        stride(anim_timer, freq, amp, 0.0f), glm::vec3(1,0,0));

    // torso crouches slightly when fleeing
    if (fleeing)
        pose.local[BONE_TORSO] = rot(pose.local[BONE_TORSO], 0.15f, glm::vec3(1,0,0));
}

void animal_anim_dog(DriverPose& pose, float anim_timer, float speed, bool fleeing){
    for (int i = 0; i < BONE_COUNT; i++)
        pose.local[i] = glm::mat4(1.0f);

    float freq = fleeing ? 3.2f : 1.6f;
    float amp  = fleeing ? 0.5f : 0.28f;

    // same diagonal trot as cat/cow
    pose.local[BONE_LEG_L] = rot(pose.local[BONE_LEG_L],
        stride(anim_timer, freq, amp, 0.0f), glm::vec3(1,0,0));
    pose.local[BONE_LEG_R] = rot(pose.local[BONE_LEG_R],
        stride(anim_timer, freq, amp, 3.14159f), glm::vec3(1,0,0));
    pose.local[BONE_ARM_L] = rot(pose.local[BONE_ARM_L],
        stride(anim_timer, freq, amp, 3.14159f), glm::vec3(1,0,0));
    pose.local[BONE_ARM_R] = rot(pose.local[BONE_ARM_R],
        stride(anim_timer, freq, amp, 0.0f), glm::vec3(1,0,0));

    // what the dog doin
    pose.local[BONE_HEAD] = rot(pose.local[BONE_HEAD],
        std::abs(stride(anim_timer, freq, 0.12f, 0.0f)), glm::vec3(1,0,0));
}