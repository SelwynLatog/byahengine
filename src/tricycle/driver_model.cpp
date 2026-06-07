#include "driver_model.hpp"
#include "driver_anim.hpp"
#include <glm/gtc/quaternion.hpp>
#include "../core/const.hpp"
#include "../renderer/obj_loader.hpp"
#include "../renderer/obj_mesh.hpp"
#include "../renderer/shader.hpp"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>
#include "../../vendor/stb/stb_image.h"
#include "../world/npc.hpp"

// cached uniform locations for driver shader
// avoids glGetUniformLocation string lookup every bone every frame
struct DriverShaderLocs {
    GLuint sid = 0;
    GLint view, proj, model, nmat, kd, usetex, tex, checker;
};
static DriverShaderLocs s_drv_locs;

static void driver_locs_update(GLuint sid){
    if (sid == s_drv_locs.sid) return;
    s_drv_locs.sid     = sid;
    s_drv_locs.view    = glGetUniformLocation(sid, "u_view");
    s_drv_locs.proj    = glGetUniformLocation(sid, "u_proj");
    s_drv_locs.model   = glGetUniformLocation(sid, "u_model");
    s_drv_locs.nmat    = glGetUniformLocation(sid, "u_normal_mat");
    s_drv_locs.kd      = glGetUniformLocation(sid, "u_kd");
    s_drv_locs.usetex  = glGetUniformLocation(sid, "u_use_texture");
    s_drv_locs.tex     = glGetUniformLocation(sid, "u_tex");
    s_drv_locs.checker = glGetUniformLocation(sid, "u_use_checker");
}

// maps bone index to the OBJ part name
static const char* bone_part_name(int bone, NpcType type) {
    switch (type) {
        case NPC_TYPE_CHICKEN:
            switch (bone) {
                case BONE_TORSO: return "chicken_body";
                case BONE_HEAD:  return "chicken_head";
                case BONE_LEG_L: return "chicken_leg_l";
                case BONE_LEG_R: return "chicken_leg_r";
                case BONE_ARM_L: return "chicken_wing_l";
                case BONE_ARM_R: return "chicken_wing_r";
                default: return "";
            }
        case NPC_TYPE_COW:
            switch (bone) {
                case BONE_TORSO: return "cow_body";
                case BONE_HEAD:  return "cow_head";
                case BONE_LEG_L: return "cow_leg_fl";
                case BONE_LEG_R: return "cow_leg_fr";
                case BONE_ARM_L: return "cow_leg_rl";
                case BONE_ARM_R: return "cow_leg_rr";
                default: return "";
            }
        case NPC_TYPE_CAT:
            switch (bone) {
                case BONE_TORSO: return "cat_body";
                case BONE_HEAD:  return "cat_head";
                case BONE_LEG_L: return "cat_leg_fl";
                case BONE_LEG_R: return "cat_leg_fr";
                case BONE_ARM_L: return "cat_leg_rl";
                case BONE_ARM_R: return "cat_leg_rr";
                default: return "";
            }
        case NPC_TYPE_DOG:
            switch (bone) {
                case BONE_TORSO: return "dog_body";
                case BONE_HEAD:  return "dog_head";
                case BONE_LEG_L: return "dog_leg_fl";
                case BONE_LEG_R: return "dog_leg_fr";
                case BONE_ARM_L: return "dog_leg_rl";
                case BONE_ARM_R: return "dog_leg_rr";
                default: return "";
            }
        default: // NPC_TYPE_PERSON and fallback
            switch (bone) {
                case BONE_TORSO: return "driver_torso";
                case BONE_HEAD:  return "driver_head";
                case BONE_LEG_L: return "driver_leg_l";
                case BONE_LEG_R: return "driver_leg_r";
                case BONE_ARM_L: return "driver_upper_arm_l";
                case BONE_ARM_R: return "driver_upper_arm_r";
                default: return "";
            }
    }
}

// compute the joint pivot from a part's bounding box
// leg/arm: pivot at top-center (hip/shoulder joint)
// head: pivot at bottom-center (neck joint)
// torso: pivot at center (anchor, not really used)
static glm::vec3 compute_pivot(int bone, glm::vec3 bmin, glm::vec3 bmax) {
    glm::vec3 top_center = { (bmin.x+bmax.x)*0.5f, (bmin.y+bmax.y)*0.5f, bmax.z };
    glm::vec3 bottom_center = { (bmin.x+bmax.x)*0.5f, (bmin.y+bmax.y)*0.5f, bmin.z };
    glm::vec3 center = { (bmin.x+bmax.x)*0.5f, (bmin.y+bmax.y)*0.5f, (bmin.z+bmax.z)*0.5f };

    switch (bone) {
        case BONE_LEG_L:
        case BONE_LEG_R: return top_center; // hip joint at top of leg
        case BONE_ARM_L:
        case BONE_ARM_R: return top_center; // shoulder at top of arm
        case BONE_HEAD: return bottom_center; // neck at bottom of head
        default: return center;
    }
}

// slice the full ObjData into per-bone ObjData structs
// each bone gets its own flat vertex buffer and a single ObjPart
static ObjData slice_part(const ObjData& full, const ObjPart& part) {
    ObjData out;
    out.materials = full.materials;

    ObjPart out_part;
    out_part.part_name = part.part_name;

    for (const auto& grp : part.groups) {
        int start = grp.vertex_start;
        int count = grp.vertex_count;
        int new_start = (int)out.vertices.size() / 8;

        for (int i = 0; i < count * 8; i++)
            out.vertices.push_back(full.vertices[start * 8 + i]);

        ObjGroup g;
        g.mat_name = grp.mat_name;
        g.vertex_start = new_start;
        g.vertex_count = count;
        out_part.groups.push_back(g);
        out.groups.push_back(g);
    }
    out.parts.push_back(out_part);
    return out;
}

void driver_model_init(DriverModel& d, const char* path, NpcType type) {
    ObjData full;
    if (!obj_load(path, full))
        std::cerr << "[driver_model] failed to load " << path << "\n";

    // compute full model bounding box for scale and foot anchor
    float minX= 1e9f, maxX=-1e9f;
    float minY= 1e9f, maxY=-1e9f;
    float minZ= 1e9f, maxZ=-1e9f;
    for (int i = 0; i < (int)full.vertices.size(); i += 8) {
        float x = full.vertices[i];
        float y = full.vertices[i+1];
        float z = full.vertices[i+2];
        minX = std::min(minX,x); maxX = std::max(maxX,x);
        minY = std::min(minY,y); maxY = std::max(maxY,y);
        minZ = std::min(minZ,z); maxZ = std::max(maxZ,z);
    }

    float model_height = maxZ - minZ;
    d.model_scale = (model_height > 0.0f) ? Const::DRIVER_TARGET_HEIGHT / model_height : 1.0f;
    d.model_center = glm::vec3((minX+maxX)*0.5f, minY, (minZ+maxZ)*0.5f);
    d.half_height = model_height * d.model_scale;
    d.model_foot_z = minZ;

    // slice and upload each bone part
    for (int b = 0; b < BONE_COUNT; b++) {
        const char* name = bone_part_name(b, type);
        const ObjPart* part = obj_find_part(full, name);
        if (!part) {
            std::cerr << "[driver_model] missing part: " << name << "\n";
            continue;
        }

        // compute per-part bounding box
        float pminX= 1e9f, pmaxX=-1e9f;
        float pminY= 1e9f, pmaxY=-1e9f;
        float pminZ= 1e9f, pmaxZ=-1e9f;
        for (const auto& grp : part->groups) {
            int base = grp.vertex_start * 8;
            for (int i = 0; i < grp.vertex_count * 8; i += 8) {
                float x = full.vertices[base+i];
                float y = full.vertices[base+i+1];
                float z = full.vertices[base+i+2];
                pminX=std::min(pminX,x); pmaxX=std::max(pmaxX,x);
                pminY=std::min(pminY,y); pmaxY=std::max(pmaxY,y);
                pminZ=std::min(pminZ,z); pmaxZ=std::max(pmaxZ,z);
            }
        }

        d.pivots[b].pivot = compute_pivot(b,
            glm::vec3(pminX, pminY, pminZ),
            glm::vec3(pmaxX, pmaxY, pmaxZ));

        ObjData sliced = slice_part(full, *part);
        obj_mesh_init(d.parts[b], std::move(sliced));
    }

    for (const auto& mat : full.materials) {
        if (mat.tex_path.empty()) continue;
        if (d.tex_cache.count(mat.tex_path)) continue;
        stbi_set_flip_vertically_on_load(1);
        int w, h, ch;
        unsigned char* px = stbi_load(mat.tex_path.c_str(), &w, &h, &ch, 4);
        if (!px) { std::cerr << "[driver] failed to load tex: " << mat.tex_path << "\n"; continue; }
        GLuint id;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(px);
        d.tex_cache[mat.tex_path] = id;
        std::cout << "[driver] tex loaded " << w << "x" << h << " " << mat.tex_path << "\n";
    }
}

void driver_model_draw(
    const DriverModel& d,
    const PlayerState& player,
    const TrikeState& trike,
    const Shader& shader,
    const glm::mat4& view,
    const glm::mat4& proj,
    const glm::quat pose_quats[BONE_COUNT],
    const glm::vec3 pose_offsets[BONE_COUNT],
    glm::vec3 pose_seat)
{
    // pick draw position and yaw
    glm::vec3 draw_pos;
    float draw_yaw;

    if (player.mode == PLAYER_DRIVING || player.mode == PLAYER_MOUNTING) {
        float c = std::cos(trike.heading), s = std::sin(trike.heading);
        draw_pos = trike.position + glm::vec3( c*pose_seat.x - s*pose_seat.z, pose_seat.y, s*pose_seat.x + c*pose_seat.z);
        draw_yaw = trike.heading;
    } 
    else {
        draw_pos = player.pos;
        draw_yaw = player.visual_yaw;
    }

    float foot_lift = -d.model_foot_z * d.model_scale;
    float bob = 0.0f;
    if (player.mode == PLAYER_FOOT && player.speed > 0.1f)
        bob = std::sin(player.anim_timer * 2.0f) * 0.03f;

    // base model matrix
    glm::mat4 base = glm::mat4(1.0f);
    base = glm::translate(base, draw_pos + glm::vec3(0.0f, foot_lift + bob, 0.0f));
    base = glm::rotate(base, -draw_yaw + glm::radians(90.0f), glm::vec3(0,1,0));
    if (player.mode == PLAYER_DRIVING || player.mode == PLAYER_MOUNTING) {
        base = glm::rotate(base, -trike.roll_angle, glm::vec3(1,0,0));
        base = glm::rotate(base, trike.pitch_angle, glm::vec3(0,0,1));
    }
    // OBJ is Z-up
    base = glm::rotate(base, glm::radians(-90.0f), glm::vec3(1,0,0));
    if (player.mode == PLAYER_FOOT) {
        float lean = glm::clamp(player.speed / 4.0f, 0.0f, 1.0f) * glm::radians(8.0f);
        base = glm::rotate(base, lean, glm::vec3(0,0,1));
    }
    glm::vec3 center_off = glm::vec3(d.model_center.x, d.model_foot_z, 0.0f);
    base = base
        * glm::translate(glm::mat4(1.0f), -center_off * d.model_scale)
        * glm::scale(glm::mat4(1.0f), glm::vec3(d.model_scale));

    // compute pose for this frame
    int anim_mode = 0;
    float mount_t = 0.0f;
    if (player.mode == PLAYER_DRIVING)  anim_mode = 1;
    if (player.mode == PLAYER_MOUNTING) {
        anim_mode = 2;
        // mount_timer counts DOWN from 0.3, so 1 - (timer/0.3) gives 0->1
        mount_t = 1.0f - glm::clamp(player.mount_timer / 0.3f, 0.0f, 1.0f);
    }

    
    DriverPose pose;
    driver_pose_compute(pose, player.anim_timer, player.speed, anim_mode, mount_t);
    // apply saved pose editor quats on top of the sit pose when driving
    if (player.mode == PLAYER_DRIVING || player.mode == PLAYER_MOUNTING) {
        for (int b = 0; b < BONE_COUNT; b++)
            pose.local[b] = pose.local[b] * glm::mat4_cast(pose_quats[b]);

        // arms tilt with steering 
        // left arm pushes forward, right pulls back on left turn
        float steer = trike.steer_angle; // radians, positive = right turn
        float arm_fwd  = steer * 0.6f;  // forward/back swing along X
        float arm_twist = steer * 0.3f; // Y twist into the turn

        glm::mat4 steer_l = glm::rotate(glm::mat4(1.0f), -arm_fwd,  glm::vec3(1,0,0));
        steer_l = glm::rotate(steer_l, -arm_twist, glm::vec3(0,1,0));
        glm::mat4 steer_r = glm::rotate(glm::mat4(1.0f),  arm_fwd,  glm::vec3(1,0,0));
        steer_r = glm::rotate(steer_r, arm_twist, glm::vec3(0,1,0));

        pose.local[BONE_ARM_L] = pose.local[BONE_ARM_L] * steer_l;
        pose.local[BONE_ARM_R] = pose.local[BONE_ARM_R] * steer_r;
    }


    driver_locs_update(shader.id);
    glUniformMatrix4fv(s_drv_locs.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(s_drv_locs.proj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform1i(s_drv_locs.checker, 0);

    // normal matrix base: inverse-transpose of base, computed once not per bone
    // bone_local is rotation only so combining is exact and cheap
    glm::mat3 base_normal = glm::mat3(glm::transpose(glm::inverse(base)));

    for (int b = 0; b < BONE_COUNT; b++) {
        const ObjMesh& mesh = d.parts[b];
        if (mesh.data.vertices.empty()) continue;

        // apply bone rotation around its pivot point in model space
        glm::vec3 piv = d.pivots[b].pivot;
        glm::mat4 bone_local = glm::mat4(1.0f);
        bone_local = glm::translate(bone_local,  piv);
        bone_local = bone_local * pose.local[b];
        bone_local = glm::translate(bone_local, -piv);
        // apply per-bone translation offset in model space
        // lets you close gaps between parts without rotation
        if (player.mode == PLAYER_DRIVING || player.mode == PLAYER_MOUNTING)
            bone_local = glm::translate(bone_local, pose_offsets[b]);

        glm::mat4 model = base * bone_local;
        // bone_local is pure rotation+translation so base_normal * bone rotation is exact
        // full inverse-transpose only needed on base (handles non-uniform editor_scale)
        glm::mat3 normal_mat = base_normal * glm::mat3(bone_local);

        glUniformMatrix4fv(s_drv_locs.model, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix3fv(s_drv_locs.nmat, 1, GL_FALSE, glm::value_ptr(normal_mat));

        glBindVertexArray(mesh.vao);
        for (const auto& part : mesh.data.parts) {
            for (const auto& grp : part.groups) {
                if (grp.vertex_count <= 0) continue;
                const ObjMaterial* mat = obj_find_material(mesh.data, grp.mat_name);
                glm::vec3 kd = mat ? mat->kd : glm::vec3(0.6f, 0.4f, 0.25f);
                glUniform3f(s_drv_locs.kd, kd.r, kd.g, kd.b);
                if (mat && !mat->tex_path.empty()) {
                    GLuint tex = 0;
                    auto it = d.tex_cache.find(mat->tex_path);
                    if (it != d.tex_cache.end()) tex = it->second;
                    if (tex) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, tex);
                        glUniform1i(s_drv_locs.tex,    0);
                        glUniform1i(s_drv_locs.usetex, 1);
                    } 
                    else {
                        glUniform1i(s_drv_locs.usetex, 0);
                    }
                } 
                else {
                    glUniform1i(s_drv_locs.usetex, 0);
                }
                glDrawArrays(GL_TRIANGLES, grp.vertex_start, grp.vertex_count);
                if (mat && !mat->tex_path.empty())
                    glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
        glBindVertexArray(0);
    }
}

void driver_model_draw_pose(
    const DriverModel& d,
    glm::vec3 seat_offset,
    const glm::quat bone_quats[BONE_COUNT],
    const glm::vec3 bone_offsets[BONE_COUNT],
    int highlight_bone,
    const Shader& shader,
    const glm::mat4& view,
    const glm::mat4& proj)
{
    float foot_lift = -d.model_foot_z * d.model_scale;

    // place driver at seat_offset with no trike roll/pitch
    glm::mat4 base = glm::mat4(1.0f);
    base = glm::translate(base, seat_offset + glm::vec3(0.0f, foot_lift, 0.0f));
    base = glm::rotate(base, glm::radians(90.0f), glm::vec3(0,1,0));
    // OBJ is Z-up
    base = glm::rotate(base, glm::radians(-90.0f), glm::vec3(1,0,0));
    glm::vec3 center_off = glm::vec3(d.model_center.x, d.model_foot_z, 0.0f);
    base = base * glm::translate(glm::mat4(1.0f), -center_off * d.model_scale) * glm::scale(glm::mat4(1.0f), glm::vec3(d.model_scale));

    // sit pose as baseline
    DriverPose pose;
    driver_pose_compute(pose, 0.0f, 0.0f, 1, 0.0f);

    glm::mat3 base_normal = glm::mat3(glm::transpose(glm::inverse(base)));

    // apply quat overrides on top of pose_sit() per bone
    // quat_mat is pre-multiplied so it rotates in bone local space
    for (int b = 0; b < BONE_COUNT; b++){
        glm::mat4 extra = glm::mat4_cast(bone_quats[b]);
        pose.local[b] = pose.local[b] * extra;
    }

    driver_locs_update(shader.id);
    glUniformMatrix4fv(s_drv_locs.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(s_drv_locs.proj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform1i(s_drv_locs.checker, 0);

    for (int b = 0; b < BONE_COUNT; b++){
        const ObjMesh& mesh = d.parts[b];
        if (mesh.data.vertices.empty()) continue;

        glm::vec3 piv = d.pivots[b].pivot;
        glm::mat4 bone_local = glm::mat4(1.0f);
        bone_local = glm::translate(bone_local,  piv);
        bone_local = bone_local * pose.local[b];
        bone_local = glm::translate(bone_local, -piv);
        // apply per-bone translation offset in model space
        // lets you close gaps between parts without rotation
        bone_local = glm::translate(bone_local, bone_offsets[b]);

        glm::mat4 model = base * bone_local;
        glm::mat3 normal_mat = base_normal * glm::mat3(bone_local);

        glUniformMatrix4fv(s_drv_locs.model, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix3fv(s_drv_locs.nmat, 1, GL_FALSE, glm::value_ptr(normal_mat));

        glBindVertexArray(mesh.vao);
        for (const auto& part : mesh.data.parts){
            for (const auto& grp : part.groups){
                if (grp.vertex_count <= 0) continue;
                const ObjMaterial* mat = obj_find_material(mesh.data, grp.mat_name);
                glm::vec3 kd = mat ? mat->kd : glm::vec3(0.6f, 0.4f, 0.25f);
                if (b == highlight_bone) {
                    kd = glm::vec3(1.0f, 0.45f, 0.05f);
                    glUniform1i(s_drv_locs.usetex, 0);
                } 
                else if (mat && !mat->tex_path.empty()) {
                    GLuint tex = 0;
                    auto it = d.tex_cache.find(mat->tex_path);
                    if (it != d.tex_cache.end()) tex = it->second;
                    if (tex) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, tex);
                        glUniform1i(s_drv_locs.tex,    0);
                        glUniform1i(s_drv_locs.usetex, 1);
                    } 
                    else {
                        glUniform1i(s_drv_locs.usetex, 0);
                    }
                } 
                else {
                    glUniform1i(s_drv_locs.usetex, 0);
                }
                glUniform3f(s_drv_locs.kd, kd.r, kd.g, kd.b);
                glDrawArrays(GL_TRIANGLES, grp.vertex_start, grp.vertex_count);
                if (b != highlight_bone && mat && !mat->tex_path.empty())
                    glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
        glBindVertexArray(0);
    }
}

void driver_model_destroy(DriverModel& d) {
    for (int b = 0; b < BONE_COUNT; b++)
        obj_mesh_destroy(d.parts[b]);
    for (auto& [path, id] : d.tex_cache)
        if (id) glDeleteTextures(1, &id);
    d.tex_cache.clear();
}