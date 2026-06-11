#pragma once
#include <glm/glm.hpp>
#include "../world/height_field.hpp"
#include "../tricycle/driver_model.hpp"
#include "../renderer/shader.hpp"
#include "../core/player_state.hpp"
#include "../physics/trike_state.hpp"

enum NpcType : int {
    NPC_TYPE_PERSON,
    NPC_TYPE_CHICKEN,
    NPC_TYPE_COW,
    NPC_TYPE_CAT,
    NPC_TYPE_DOG,
    NPC_TYPE_TIKBALANG
};

// map enum to names so easier to set up in editor
// add more npcs here
static constexpr const char* NPC_TYPE_NAMES[] = {
    "PERSON", 
    "CHICKEN", 
    "COW", 
    "CAT",
    "DOG",
    "TIKBALANG"
};

static constexpr int NPC_TYPE_COUNT = sizeof(NPC_TYPE_NAMES) / sizeof(NPC_TYPE_NAMES[0]);

enum NpcMode {
    NPC_IDLE,
    NPC_WALK,
    NPC_HAILING,
    NPC_MOUNTING,
    NPC_PASSENGER,
    NPC_DISMOUNTING,
    NPC_RAGDOLL,
    NPC_FLEE,
    NPC_GRAZE
};

struct NpcState {
    int id = -1;
    NpcType type = NPC_TYPE_PERSON;
    glm::vec3 position = glm::vec3(0.0f);
    float yaw = 0.0f;
    float speed = 0.0f;
    float anim_timer = 0.0f;
    NpcMode mode = NPC_IDLE;

    // walk patrol
    glm::vec3 walk_a = glm::vec3(0.0f);
    glm::vec3 walk_b = glm::vec3(0.0f);
    bool walk_forward = true;

    // hailing
    bool can_hail = false;       // set from editor, only PERSON type
    float hail_timer = 0.0f;     // countdown until next hail attempt
    glm::vec3 drop_point = glm::vec3(0.0f); // destination set in editor

    // passenger
    float fare_distance = 0.0f;  // accumulated while riding
    float weight = 60.0f;        // kg, affects trike physics

    // ragdoll
    glm::vec3 ragdoll_vel = glm::vec3(0.0f);
    float ragdoll_timer = 0.0f;
    float ragdoll_pitch = 0.0f;
    float ragdoll_roll = 0.0f;
    float ragdoll_pitch_vel = 0.0f;
    float ragdoll_roll_vel = 0.0f;
    float ragdoll_yaw_vel = 0.0f;
    float ragdoll_yaw = 0.0f;

    // per-limb flop 
    // only arms and legs, torso/head stay with root
    float limb_pitch[4] = {};     // LEG_L, LEG_R, ARM_L, ARM_R
    float limb_roll[4] = {};
    float limb_pitch_vel[4] = {};
    float limb_roll_vel[4] = {};

    // future
    int voiceline_id = -1;
    std::string model_path = ""; // filename from WorldObject, used to look up model in app
    glm::vec3 editor_scale = glm::vec3(1.0f);
    float editor_yaw = 0.0f;
    float editor_y_floor_offset = 0.0f;
    float spawn_yaw = 0.0f; // yaw at init time, used to compute facing offset
    
    float hail_wave_timer = 0.0f;
    float yap_timer = 0.0f; // countdown until next idle ambient yapping
    float flee_timer = 0.0f; // countdown until animal resumes normal mode
    float idle_vary_timer = 0.0f; // countdown until next idle->walk transition

    glm::vec3 hail_pose_seat = glm::vec3(0.0f);
    glm::quat hail_pose_quat[BONE_COUNT];
    glm::vec3 hail_pose_offset[BONE_COUNT] = {};

    glm::vec3 mount_pose_seat = glm::vec3(0.0f);
    glm::quat mount_pose_quat[BONE_COUNT];
    glm::vec3 mount_pose_offset[BONE_COUNT] = {};
};

void npc_init(NpcState& npc, int id, NpcType type, glm::vec3 pos, float yaw,
              glm::vec3 walk_a, glm::vec3 walk_b,
              bool can_hail, glm::vec3 drop_point, float weight);
void npc_update(NpcState& npc, const HeightField& terrain, float dt,
    glm::vec3 trike_pos = glm::vec3(0.0f));
void npc_hit(NpcState& npc, glm::vec3 impulse);
void npc_draw(const NpcState& npc, const DriverModel& model,
              const Shader& shader, const glm::mat4& view, const glm::mat4& proj);