#include "npc.hpp"
#include "../core/const.hpp"
#include "../world/height_field.hpp"
#include "../tricycle/driver_model.hpp"
#include "../tricycle/driver_anim.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

static constexpr float NPC_WALK_SPEED = 1.4f;
static constexpr float NPC_ARRIVE_DIST = 0.6f;
static constexpr float NPC_RAGDOLL_DURATION = 3.5f;
static constexpr float NPC_GRAVITY = 9.81f;
static constexpr float NPC_ANG_DRAG = 0.85f;
static constexpr float NPC_LIN_DRAG = 0.75f;
static constexpr float NPC_HAIL_RANGE_SQ = 36.0f; // 6m radius, checked in app
static constexpr float NPC_HAIL_TIMER_MIN = 8.0f;  // seconds between hail attempts
static constexpr float NPC_HAIL_TIMER_MAX = 20.0f;

void npc_init(NpcState& npc, int id, NpcType type, glm::vec3 pos, float yaw,
              glm::vec3 walk_a, glm::vec3 walk_b,
              bool can_hail, glm::vec3 drop_point, float weight) {
    npc.id = id;
    npc.type = type;
    npc.position = pos;
    npc.yaw = yaw;
    npc.walk_a = walk_a;
    npc.walk_b = walk_b;
    npc.can_hail = can_hail && (type == NPC_TYPE_PERSON);
    npc.drop_point = drop_point;
    npc.weight = weight;
    npc.mode = (glm::length(walk_b - walk_a) > 0.1f) ? NPC_WALK : NPC_IDLE;
    npc.walk_forward = true;
    npc.anim_timer = 0.0f;
    // stagger hail timers so npcs don't all hail at once
    npc.hail_timer= NPC_HAIL_TIMER_MIN + (float)(id % 7) * 1.8f;
}

void npc_update(NpcState& npc, const HeightField& terrain, float dt) {

    if (npc.mode == NPC_RAGDOLL) {
        npc.ragdoll_timer += dt;

        npc.ragdoll_vel *= std::pow(NPC_LIN_DRAG, dt);
        npc.position += npc.ragdoll_vel * dt;

        float ground_y = heightfield_sample(terrain, npc.position.x, npc.position.z);
        if (npc.position.y > ground_y)
            npc.position.y -= NPC_GRAVITY * dt * dt;
        if (npc.position.y < ground_y)
            npc.position.y = ground_y;

        float ang_drag = std::pow(NPC_ANG_DRAG, dt);
        npc.ragdoll_pitch_vel *= ang_drag;
        npc.ragdoll_roll_vel  *= ang_drag;
        npc.ragdoll_pitch += npc.ragdoll_pitch_vel * dt;
        npc.ragdoll_roll += npc.ragdoll_roll_vel  * dt;

        if (std::abs(npc.ragdoll_pitch_vel) < 0.05f) npc.ragdoll_pitch *= (1.0f - 4.0f * dt);
        if (std::abs(npc.ragdoll_roll_vel)  < 0.05f) npc.ragdoll_roll  *= (1.0f - 4.0f * dt);

        if (npc.ragdoll_timer >= NPC_RAGDOLL_DURATION) {
            npc.ragdoll_timer = 0.0f;
            npc.ragdoll_pitch = npc.ragdoll_roll     = 0.0f;
            npc.ragdoll_pitch_vel = npc.ragdoll_roll_vel = 0.0f;
            npc.ragdoll_vel = glm::vec3(0.0f);
            npc.mode = (glm::length(npc.walk_b - npc.walk_a) > 0.1f) ? NPC_WALK : NPC_IDLE;
        }
        return;
    }

    // passenger: position locked to trike in app, nothing to update here
    if (npc.mode == NPC_PASSENGER) return;

    if (npc.mode == NPC_HAILING) {
        // stand and face the trike direction 
        // handled in app via yaw update
        npc.speed = 0.0f;
        npc.anim_timer += dt;
        return;
    }

    if (npc.mode == NPC_MOUNTING) {
        // walk toward sidecar mount point — app sets target, we just walk
        npc.speed = NPC_WALK_SPEED;
        npc.anim_timer += NPC_WALK_SPEED * dt * 1.8f;
        npc.position.y = heightfield_sample(terrain, npc.position.x, npc.position.z);
        return;
    }

    if (npc.mode == NPC_DISMOUNTING) {
        // walk away from drop point toward walk_a, then resume normal mode
        glm::vec3 delta = npc.walk_a - npc.position;
        delta.y = 0.0f;
        float dist = glm::length(delta);
        if (dist < NPC_ARRIVE_DIST) {
            npc.fare_distance = 0.0f;
            npc.mode = (glm::length(npc.walk_b - npc.walk_a) > 0.1f) ? NPC_WALK : NPC_IDLE;
            npc.hail_timer = NPC_HAIL_TIMER_MIN; // reset hail cooldown
        } 
        else {
            glm::vec3 dir = delta / dist;
            npc.yaw = std::atan2(dir.z, dir.x);
            npc.position += dir * NPC_WALK_SPEED * dt;
        }
        npc.speed = NPC_WALK_SPEED;
        npc.anim_timer += NPC_WALK_SPEED * dt * 1.8f;
        npc.position.y = heightfield_sample(terrain, npc.position.x, npc.position.z);
        return;
    }

    if (npc.mode == NPC_WALK) {
        glm::vec3 target = npc.walk_forward ? npc.walk_b : npc.walk_a;
        glm::vec3 delta  = target - npc.position;
        delta.y = 0.0f;
        float dist = glm::length(delta);

        if (dist < NPC_ARRIVE_DIST) {
            npc.walk_forward = !npc.walk_forward;
        } 
        else {
            glm::vec3 dir = delta / dist;
            npc.yaw = std::atan2(dir.z, dir.x);
            npc.position += dir * NPC_WALK_SPEED * dt;
        }
        npc.speed = NPC_WALK_SPEED;
        npc.anim_timer += NPC_WALK_SPEED * dt * 1.8f;
    }
    else { // NPC_IDLE
        npc.speed = 0.0f;
        npc.anim_timer += dt;

        // tick hail cooldown for idle persons
        if (npc.can_hail && npc.hail_timer > 0.0f)
            npc.hail_timer -= dt;
    }

    npc.position.y = heightfield_sample(terrain, npc.position.x, npc.position.z);
}

void npc_hit(NpcState& npc, glm::vec3 impulse) {
    if (npc.mode == NPC_RAGDOLL || npc.mode == NPC_PASSENGER) return;
    npc.mode = NPC_RAGDOLL;
    npc.ragdoll_timer = 0.0f;
    npc.ragdoll_vel = impulse;
    float speed = glm::length(impulse);
    npc.ragdoll_pitch_vel = speed * 1.2f;
    npc.ragdoll_roll_vel  = speed * 0.6f * (impulse.x >= 0.0f ? 1.0f : -1.0f);
}

void npc_draw(
    const NpcState& npc,
    const DriverModel& model,
    const Shader& shader,
    const glm::mat4& view,
    const glm::mat4& proj)
{
    PlayerState fake;
    fake.mode = PLAYER_FOOT;
    fake.pos = npc.position;
    fake.yaw = npc.yaw;
    fake.speed = npc.speed;
    fake.anim_timer = npc.anim_timer;

    static const glm::quat identity_quats[BONE_COUNT] = {
        glm::quat(1,0,0,0), glm::quat(1,0,0,0), glm::quat(1,0,0,0),
        glm::quat(1,0,0,0), glm::quat(1,0,0,0), glm::quat(1,0,0,0)
    };
    static const glm::vec3 zero_offsets[BONE_COUNT] = {};
    static const glm::vec3 no_seat = glm::vec3(0.0f);

    TrikeState fake_trike{};
    if (npc.mode == NPC_RAGDOLL) {
        fake.mode = PLAYER_DRIVING; // use driving path so roll/pitch are applied
        fake.speed = 0.0f;
        fake.anim_timer = 0.0f;
        fake_trike.position = npc.position;
        fake_trike.heading = npc.yaw;
        fake_trike.roll_angle = npc.ragdoll_roll;
        fake_trike.pitch_angle = npc.ragdoll_pitch;
    }

    driver_model_draw(model, fake, fake_trike, shader, view, proj,
        identity_quats, zero_offsets, no_seat);
}