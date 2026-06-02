#pragma once
#include <glm/glm.hpp>

// bone indices
enum BoneIdx {
    BONE_TORSO     = 0,
    BONE_HEAD      = 1,
    BONE_LEG_L     = 2,
    BONE_LEG_R     = 3,
    BONE_ARM_L     = 4,
    BONE_ARM_R     = 5,
    BONE_COUNT     = 6
};

// pivot point for a single bone in model space
// all rotations applied around this point
struct BonePivot {
    glm::vec3 pivot = glm::vec3(0.0f); // joint position in model space
};

// one pose = one local rotation matrix per bone
// applied on top of the base model transform in driver_model_draw
struct DriverPose {
    glm::mat4 local[BONE_COUNT]; // initialized to identity
};

// fills pose for the current frame
// anim_timer: accumulated walk cycle timer (seconds * speed)
// speed: scalar m/s, used to scale swing amplitude
// mode: 0 = foot walking, 1 = sitting/driving, 2 = mounting (blend)
// mount_t: 0..1, used when mode == 2 to lerp walk->sit
void driver_pose_compute(DriverPose& pose, float anim_timer,
                         float speed, int mode, float mount_t);