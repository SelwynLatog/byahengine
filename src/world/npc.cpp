#include "npc.hpp"
#include "../core/const.hpp"
#include "../world/height_field.hpp"
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
            npc.yaw = std::atan2(-dir.z, -dir.x);
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
            npc.yaw = std::atan2(-dir.z, -dir.x);
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
    // build base matrix the same way editor_renderer_draw_props does:
    // translate to position, rotate by placed yaw, apply y_floor_offset, scale
    // then append the Z-up OBJ correction and bone scale on top
    glm::mat4 base = glm::mat4(1.0f);
    base = glm::translate(base, npc.position);

    // facing: patrol walk overwrites yaw each frame, editor_yaw is the initial facing
    // use npc.yaw for live direction, but keep editor_yaw as the neutral rest facing
    float draw_yaw = npc.yaw + model.forward_offset;
    base = glm::rotate(base, draw_yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    base = glm::translate(base, glm::vec3(0.0f, npc.editor_y_floor_offset, 0.0f));

    if (npc.mode == NPC_RAGDOLL) {
        base = glm::rotate(base, npc.ragdoll_pitch, glm::vec3(1.0f, 0.0f, 0.0f));
        base = glm::rotate(base, npc.ragdoll_roll,  glm::vec3(0.0f, 0.0f, 1.0f));
    }


    glm::vec3 center_off = glm::vec3(model.model_center.x, model.model_foot_z, 0.0f);
    base = base
        * glm::translate(glm::mat4(1.0f), -center_off)
        * glm::scale(glm::mat4(1.0f), npc.editor_scale);
    // compute walk/idle pose
    int anim_mode = (npc.mode == NPC_RAGDOLL) ? 1 : 0; // sit pose freezes on ragdoll
    DriverPose pose;
    driver_pose_compute(pose, npc.anim_timer, npc.speed, anim_mode, 0.0f);

    static const glm::quat identity_quats[BONE_COUNT] = {
        glm::quat(1,0,0,0), glm::quat(1,0,0,0), glm::quat(1,0,0,0),
        glm::quat(1,0,0,0), glm::quat(1,0,0,0), glm::quat(1,0,0,0)
    };
    static const glm::vec3 zero_offsets[BONE_COUNT] = {};

    // set shared shader uniforms
    glUniformMatrix4fv(glGetUniformLocation(shader.id, "u_view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader.id, "u_proj"), 1, GL_FALSE, glm::value_ptr(proj));
    glUniform1i(glGetUniformLocation(shader.id, "u_use_checker"), 0);

    for (int b = 0; b < BONE_COUNT; b++) {
        const ObjMesh& mesh = model.parts[b];
        if (mesh.data.vertices.empty()) continue;

        glm::vec3 piv = model.pivots[b].pivot;
        glm::mat4 bone_local = glm::translate(glm::mat4(1.0f), piv);
        bone_local = bone_local * pose.local[b];
        bone_local = glm::translate(bone_local, -piv);

        glm::mat4 final_model = base * bone_local;
        glm::mat3 normal_mat = glm::mat3(glm::transpose(glm::inverse(final_model)));

        glUniformMatrix4fv(glGetUniformLocation(shader.id, "u_model"), 1, GL_FALSE, glm::value_ptr(final_model));
        glUniformMatrix3fv(glGetUniformLocation(shader.id, "u_normal_mat"), 1, GL_FALSE, glm::value_ptr(normal_mat));

        glBindVertexArray(mesh.vao);
        for (const auto& part : mesh.data.parts) {
            for (const auto& grp : part.groups) {
                if (grp.vertex_count <= 0) continue;
                const ObjMaterial* mat = obj_find_material(mesh.data, grp.mat_name);
                glm::vec3 kd = mat ? mat->kd : glm::vec3(0.8f);
                glUniform3f(glGetUniformLocation(shader.id, "u_kd"), kd.r, kd.g, kd.b);
                if (mat && !mat->tex_path.empty()) {
                    // tex is already in model.tex_cache from driver_model_init
                    auto it = model.tex_cache.find(mat->tex_path);
                    GLuint tex = (it != model.tex_cache.end()) ? it->second : 0;
                    if (tex) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, tex);
                        glUniform1i(glGetUniformLocation(shader.id, "u_tex"), 0);
                        glUniform1i(glGetUniformLocation(shader.id, "u_use_texture"), 1);
                    } else {
                        glUniform1i(glGetUniformLocation(shader.id, "u_use_texture"), 0);
                    }
                } else {
                    glUniform1i(glGetUniformLocation(shader.id, "u_use_texture"), 0);
                }
                glDrawArrays(GL_TRIANGLES, grp.vertex_start, grp.vertex_count);
                if (mat && !mat->tex_path.empty())
                    glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
        glBindVertexArray(0);
    }
}