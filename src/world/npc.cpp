#include "npc.hpp"
#include "../core/const.hpp"
#include "../world/height_field.hpp"
#include "../world/animal_behavior.hpp"
#include "../world/animal_anim.hpp"
#include "../tricycle/driver_model.hpp"
#include "../tricycle/driver_anim.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include "../renderer/obj_mesh.hpp"
#include "../renderer/obj_loader.hpp"
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


static void animal_update(NpcState& npc, const HeightField& terrain,
    const AnimalBehavior& b, float dt, glm::vec3 trike_pos)
{
    // startle check 
    // any non-ragdoll animal flees if trike is too close
    if (npc.mode != NPC_RAGDOLL && npc.mode != NPC_FLEE){
        glm::vec3 d = npc.position - trike_pos;
        d.y = 0.0f;
        if (glm::dot(d, d) < b.startle_range * b.startle_range){
            npc.mode = NPC_FLEE;
            npc.flee_timer = b.startle_duration;
            // flee direction: directly away from trike
            float len = glm::length(d);
            if (len > 0.1f){
                glm::vec3 dir = d / len;
                npc.yaw = std::atan2(dir.z, dir.x);
            }
        }
    }

    if (npc.mode == NPC_FLEE){
        npc.flee_timer -= dt;
        if (npc.flee_timer <= 0.0f){
            // resume patrol or idle
            npc.mode = (glm::length(npc.walk_b - npc.walk_a) > 0.1f)
                ? NPC_WALK : NPC_IDLE;
            npc.idle_vary_timer = b.idle_time_min
                + (float)(npc.id % 7) / 7.0f * (b.idle_time_max - b.idle_time_min);
        }
        else {
            // run in flee direction
            glm::vec3 fwd = { std::cos(npc.yaw), 0.0f, std::sin(npc.yaw) };
            npc.position += fwd * b.flee_speed * dt;
            npc.speed = b.flee_speed;
            npc.anim_timer += b.flee_speed * dt * 2.2f;
            npc.position.y = heightfield_sample(terrain, npc.position.x, npc.position.z);
        }
        return;
    }

    if (npc.mode == NPC_GRAZE){
        npc.speed = 0.0f;
        npc.anim_timer += dt;
        npc.idle_vary_timer -= dt;
        if (npc.idle_vary_timer <= 0.0f){
            // switch to walk for a bit then back
            npc.mode = NPC_WALK;
            npc.idle_vary_timer = b.idle_time_min
                + (float)(npc.id % 5) / 5.0f * (b.idle_time_max - b.idle_time_min);
        }
        return;
    }

    if (npc.mode == NPC_WALK){
        glm::vec3 target = npc.walk_forward ? npc.walk_b : npc.walk_a;
        glm::vec3 delta = target - npc.position;
        delta.y = 0.0f;
        float dist = glm::length(delta);

        if (dist < 0.6f){
            npc.walk_forward = !npc.walk_forward;
            // after arriving, idle for a bit before turning back
            npc.mode = b.grazes ? NPC_GRAZE : NPC_IDLE;
            npc.idle_vary_timer = b.idle_time_min
                + (float)(npc.id % 7) / 7.0f * (b.idle_time_max - b.idle_time_min);
        }
        else {
            glm::vec3 dir = delta / dist;
            npc.yaw = std::atan2(dir.z, dir.x);
            npc.position += dir * b.walk_speed * dt;
        }
        npc.speed = b.walk_speed;
        npc.anim_timer += b.walk_speed * dt * 1.8f;
        npc.position.y = heightfield_sample(terrain, npc.position.x, npc.position.z);
        return;
    }

    // NPC_IDLE
    npc.speed = 0.0f;
    npc.anim_timer += dt;
    npc.idle_vary_timer -= dt;
    if (npc.idle_vary_timer <= 0.0f && glm::length(npc.walk_b - npc.walk_a) > 0.1f){
        npc.mode = NPC_WALK;
        npc.idle_vary_timer = b.idle_time_min
            + (float)(npc.id % 7) / 7.0f * (b.idle_time_max - b.idle_time_min);
    }
}

void npc_init(NpcState& npc, int id, NpcType type, glm::vec3 pos, float yaw,
              glm::vec3 walk_a, glm::vec3 walk_b,
              bool can_hail, glm::vec3 drop_point, float weight) {
    npc.id = id;
    npc.type = type;
    npc.position = pos;
    npc.yaw = yaw;
    npc.spawn_yaw = npc.yaw;
    npc.walk_a = walk_a;
    npc.walk_b = walk_b;
    npc.can_hail = can_hail && (type == NPC_TYPE_PERSON);
    npc.drop_point = drop_point;
    npc.weight = weight;
    npc.mode = (glm::length(walk_b - walk_a) > 0.1f) ? NPC_WALK : NPC_IDLE;
    npc.walk_forward = true;
    npc.anim_timer = 0.0f;
    glm::vec3 walk_dir = walk_b - walk_a;
    walk_dir.y = 0.0f;
    if (glm::length(walk_dir) > 0.1f)
        npc.yaw = std::atan2(walk_dir.z, walk_dir.x);
    // stagger hail timers so npcs don't all hail at once
    npc.hail_timer= NPC_HAIL_TIMER_MIN + (float)(id % 7) * 1.8f;
    for (int i = 0; i < BONE_COUNT; i++){
        npc.hail_pose_quat[i]  = glm::quat(1,0,0,0);
        npc.mount_pose_quat[i] = glm::quat(1,0,0,0);
    }
}

void npc_update(NpcState& npc, const HeightField& terrain, float dt,
    glm::vec3 trike_pos)
{
    // animal dispatch 
    // runs separate behavior from human NPCs
    if (npc_type_is_animal(npc.type)){
        const AnimalBehavior& b = ANIMAL_BEHAVIOR_TABLE[(int)npc.type];
        animal_update(npc, terrain, b, dt, trike_pos);
        return;
    }

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

        npc.ragdoll_yaw_vel *= ang_drag;
        npc.ragdoll_yaw += npc.ragdoll_yaw_vel * dt;
        if (std::abs(npc.ragdoll_yaw_vel) < 0.05f) npc.ragdoll_yaw *= (1.0f - 4.0f * dt);

        if (npc.ragdoll_timer >= NPC_RAGDOLL_DURATION) {
            npc.ragdoll_timer = 0.0f;
            npc.ragdoll_pitch = npc.ragdoll_roll     = 0.0f;
            npc.ragdoll_pitch_vel = npc.ragdoll_roll_vel = 0.0f;
            npc.ragdoll_vel = glm::vec3(0.0f);
            npc.ragdoll_yaw = npc.ragdoll_yaw_vel = 0.0f;
            for (int i = 0; i < 4; i++)
                npc.limb_pitch[i] = npc.limb_roll[i] = npc.limb_pitch_vel[i] = npc.limb_roll_vel[i] = 0.0f;
            npc.mode = (glm::length(npc.walk_b - npc.walk_a) > 0.1f) ? NPC_WALK : NPC_IDLE;
            return;
        }
        // limb flop: spring-damper chasing zero (gravity pulls them down)
        // indices: 0=LEG_L, 1=LEG_R, 2=ARM_L, 3=ARM_R
        static constexpr float limb_stiff[4] = { 2.5f, 2.5f, 3.5f, 3.5f };
        static constexpr float limb_damp[4]  = { 1.2f, 1.2f, 1.8f, 1.8f };
        for (int i = 0; i < 4; i++) {
            // spring toward zero (rest pose) with damping
            npc.limb_pitch_vel[i] += (-npc.limb_pitch[i] * limb_stiff[i] - npc.limb_pitch_vel[i] * limb_damp[i]) * dt;
            npc.limb_roll_vel[i]  += (-npc.limb_roll[i]  * limb_stiff[i] - npc.limb_roll_vel[i]  * limb_damp[i]) * dt;
            npc.limb_pitch[i] += npc.limb_pitch_vel[i] * dt;
            npc.limb_roll[i]  += npc.limb_roll_vel[i]  * dt;
        }
        return;
    }

    // passenger: position locked to trike in app, nothing to update here
    if (npc.mode == NPC_PASSENGER) return;

    if (npc.mode == NPC_HAILING) {
        // stand still, face trike (yaw set in app)
        // hail_wave_timer drives the para po arm wave independently
        npc.speed = 0.0f;
        npc.anim_timer += dt;
        npc.hail_wave_timer += dt;
        return;
    }

    if (npc.mode == NPC_MOUNTING) {
        // walk toward sidecar mount point
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
    npc.ragdoll_roll_vel = speed * 0.6f * (impulse.x >= 0.0f ? 1.0f : -1.0f);
    npc.ragdoll_yaw_vel = speed * 0.4f * (impulse.z >= 0.0f ? 1.0f : -1.0f);

    // animal ragdoll
    // generic ragdoll but works for now
    // kick limbs outward 
    // arms fly up, legs splay
    float side = (impulse.x >= 0.0f ? 1.0f : -1.0f);
    npc.limb_pitch_vel[0] =  speed * 2.8f;  // LEG_L forward
    npc.limb_pitch_vel[1] =  speed * 2.2f;  // LEG_R forward
    npc.limb_roll_vel[0]  = -speed * 1.5f * side;
    npc.limb_roll_vel[1]  =  speed * 1.5f * side;
    npc.limb_pitch_vel[2] =  speed * 3.5f;  // ARM_L flings up
    npc.limb_pitch_vel[3] =  speed * 3.0f;  // ARM_R flings up
    npc.limb_roll_vel[2]  = -speed * 2.0f * side;
    npc.limb_roll_vel[3]  =  speed * 2.0f * side;   
}

void npc_draw(
    const NpcState& npc,
    const DriverModel& model,
    const Shader& shader,
    const glm::mat4& view,
    const glm::mat4& proj)
{
    // build base matrix the same way editor_renderer_draw_props does:
    // translate to position, rotate by placed yaw, apply y_floor_offset, scale
    // then append the Z-up OBJ correction and bone scale on top
    glm::mat4 base = glm::mat4(1.0f);
    base = glm::translate(base, npc.position);

    // facing: patrol walk overwrites yaw each frame, editor_yaw is the initial facing
    // use npc.yaw for live direction, but keep editor_yaw as the neutral rest facing
    base = glm::rotate(base, -npc.yaw + glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    if (npc.mode == NPC_RAGDOLL) {
        base = glm::rotate(base, npc.ragdoll_pitch, glm::vec3(1.0f, 0.0f, 0.0f));
        base = glm::rotate(base, npc.ragdoll_roll,  glm::vec3(0.0f, 0.0f, 1.0f));
    }


    glm::vec3 center_off = glm::vec3(model.model_center.x, 0.0f, 0.0f);
    base = base
        * glm::translate(glm::mat4(1.0f), -center_off)
        * glm::scale(glm::mat4(1.0f), npc.editor_scale);
    DriverPose pose;
    const glm::quat* override_quat   = nullptr;
    const glm::vec3* override_offset = nullptr;
    glm::vec3 override_seat = glm::vec3(0.0f);
    bool use_override = false;

    if (npc.mode == NPC_HAILING){
        driver_pose_compute(pose, npc.anim_timer, 0.0f, 0, 0.0f); // idle/stand base as default
        override_quat   = npc.hail_pose_quat;
        override_offset = npc.hail_pose_offset;
        override_seat   = npc.hail_pose_seat;
        use_override    = true;
    }
    else if (npc.mode == NPC_PASSENGER || npc.mode == NPC_MOUNTING){
        driver_pose_compute(pose, npc.anim_timer, 0.0f, 1, 0.0f); // sit base
        override_quat   = npc.mount_pose_quat;
        override_offset = npc.mount_pose_offset;
        override_seat   = npc.mount_pose_seat;
        use_override    = true;
    }
    else if (npc_type_is_animal(npc.type)){
        bool fleeing = (npc.mode == NPC_FLEE);
        bool grazing = (npc.mode == NPC_GRAZE);
        switch (npc.type){
            case NPC_TYPE_CHICKEN:
                animal_anim_chicken(pose, npc.anim_timer, npc.speed, fleeing);
                break;
            case NPC_TYPE_COW:
                animal_anim_cow(pose, npc.anim_timer, npc.speed, fleeing, grazing);
                break;
            case NPC_TYPE_CAT:
                animal_anim_cat(pose, npc.anim_timer, npc.speed, fleeing);
                break;
            case NPC_TYPE_DOG:
                animal_anim_dog(pose, npc.anim_timer, npc.speed, fleeing);
                break;
            default:
                driver_pose_compute(pose, npc.anim_timer, npc.speed, 0, 0.0f);
                break;
        }
    }
    else {
        int anim_mode = (npc.mode == NPC_RAGDOLL) ? 1 : 0;
        driver_pose_compute(pose, npc.anim_timer, npc.speed, anim_mode, 0.0f);
    }

    // cache uniform locations
    GLuint sid = shader.id;
    static GLuint cached_sid = 0;
    static GLint loc_view, loc_proj, loc_model, loc_nmat, loc_kd, loc_usetex, loc_tex, loc_checker;
    if (sid != cached_sid){
        cached_sid  = sid;
        loc_view    = glGetUniformLocation(sid, "u_view");
        loc_proj    = glGetUniformLocation(sid, "u_proj");
        loc_model   = glGetUniformLocation(sid, "u_model");
        loc_nmat    = glGetUniformLocation(sid, "u_normal_mat");
        loc_kd      = glGetUniformLocation(sid, "u_kd");
        loc_usetex  = glGetUniformLocation(sid, "u_use_texture");
        loc_tex     = glGetUniformLocation(sid, "u_tex");
        loc_checker = glGetUniformLocation(sid, "u_use_checker");
    }

    glUniformMatrix4fv(loc_view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(loc_proj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform1i(loc_checker, 0);

    for (int b = 0; b < BONE_COUNT; b++) {
        const ObjMesh& mesh = model.parts[b];
        if (mesh.data.vertices.empty()) continue;

        glm::vec3 piv = model.pivots[b].pivot * npc.editor_scale;
        glm::mat4 bone_local = glm::translate(glm::mat4(1.0f), piv);

        if (use_override){
            // apply sit base then override quat and offset from saved pose
            bone_local = bone_local * pose.local[b];
            bone_local = bone_local * glm::mat4_cast(override_quat[b]);
            if (glm::length(override_offset[b]) > 0.0001f)
                bone_local = glm::translate(bone_local, override_offset[b]);
        }
        else {
            bone_local = bone_local * pose.local[b];
        }

        // limb flop in ragdoll 
        // rotate around pivot, body stays connected
        if (npc.mode == NPC_RAGDOLL) {
            int li = -1;
            if (b == BONE_LEG_L) li = 0;
            else if (b == BONE_LEG_R) li = 1;
            else if (b == BONE_ARM_L) li = 2;
            else if (b == BONE_ARM_R) li = 3;
            if (li >= 0) {
                bone_local = glm::rotate(bone_local, npc.limb_pitch[li], glm::vec3(1,0,0));
                bone_local = glm::rotate(bone_local, npc.limb_roll[li],  glm::vec3(0,0,1));
            }
        }

        bone_local = glm::translate(bone_local, -piv);

        glm::mat4 final_model = base * bone_local;
        glm::mat3 normal_mat = glm::mat3(final_model);

        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(final_model));
        glUniformMatrix3fv(loc_nmat,  1, GL_FALSE, glm::value_ptr(normal_mat));

        glBindVertexArray(mesh.vao);
        for (const auto& part : mesh.data.parts) {
            for (const auto& grp : part.groups) {
                if (grp.vertex_count <= 0) continue;
                const ObjMaterial* mat = obj_find_material(mesh.data, grp.mat_name);
                glm::vec3 kd = mat ? mat->kd : glm::vec3(0.8f);
                glUniform3f(loc_kd, kd.r, kd.g, kd.b);
                if (mat && !mat->tex_path.empty()) {
                    // tex is already in model.tex_cache from driver_model_init
                    auto it = model.tex_cache.find(mat->tex_path);
                    GLuint tex = (it != model.tex_cache.end()) ? it->second : 0;
                    if (tex) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, tex);
                        glUniform1i(loc_tex, 0);
                        glUniform1i(loc_usetex, 1);
                    } 
                    else {
                        glUniform1i(loc_usetex, 0);
                    }
                } 
                else {
                    glUniform1i(loc_usetex, 0);
                }
                glDrawArrays(GL_TRIANGLES, grp.vertex_start, grp.vertex_count);
                if (mat && !mat->tex_path.empty())
                    glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
        glBindVertexArray(0);
    }
}