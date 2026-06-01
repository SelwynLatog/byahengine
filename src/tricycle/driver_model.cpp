#include "driver_model.hpp"
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

static void set_mat4(const Shader& s, const char* n, const glm::mat4& m){
    glUniformMatrix4fv(glGetUniformLocation(s.id, n), 1, GL_FALSE, glm::value_ptr(m));
}
static void set_mat3(const Shader& s, const char* n, const glm::mat3& m){
    glUniformMatrix3fv(glGetUniformLocation(s.id, n), 1, GL_FALSE, glm::value_ptr(m));
}
static void set_vec3(const Shader& s, const char* n, glm::vec3 v){
    glUniform3f(glGetUniformLocation(s.id, n), v.x, v.y, v.z);
}

void driver_model_init(DriverModel& d){
    ObjData data;
    if (!obj_load("../assets/people/DRIVER.obj", data))
        std::cerr << "[driver_model] failed to load DRIVER.obj\n";
    // compute bounding box BEFORE move
    float minX= 1e9f, maxX=-1e9f;
    float minY= 1e9f, maxY=-1e9f;
    float minZ= 1e9f, maxZ=-1e9f;
    for (int i = 0; i < (int)data.vertices.size(); i += 8){
        float x = data.vertices[i];
        float y = data.vertices[i+1];
        float z = data.vertices[i+2];
        minX=std::min(minX,x); maxX=std::max(maxX,x);
        minY=std::min(minY,y); maxY=std::max(maxY,y);
        minZ=std::min(minZ,z); maxZ=std::max(maxZ,z);
    }
    // blender sucks, its retarded. It feels like driving a helicopter except most controls
    // fail occasionally. Added debug prints since diff models genuinely have retarded positions,
    // even when your axis exports are the same, your program has to adjust to its retardedness
    std::cout << "[driver] bounds X:" << (maxX-minX) << " Y:" << (maxY-minY) << " Z:" << (maxZ-minZ) << "\n";

    obj_mesh_init(d.mesh, std::move(data));
    float model_height = maxZ - minZ; 
    d.model_scale = (model_height > 0.0f) ? Const::DRIVER_TARGET_HEIGHT / model_height : 1.0f;
    // center XZ, floor at minY so feet anchor at origin in model space
    d.model_center = glm::vec3((minX+maxX)*0.5f, minY, (minZ+maxZ)*0.5f);
    d.half_height = model_height * d.model_scale;
    d.model_foot_z = minZ;
}

void driver_model_draw(
    const DriverModel& d,
    const PlayerState& player,
    const TrikeState& trike,
    const Shader& shader,
    const glm::mat4& view,
    const glm::mat4& proj)
{
    if (d.mesh.data.vertices.empty()) return;

    glm::vec3 draw_pos;
    float draw_yaw;

    if (player.mode == PLAYER_DRIVING || player.mode == PLAYER_MOUNTING){
        float c = std::cos(trike.heading), s = std::sin(trike.heading);
        glm::vec3 seat_local = {
            Const::DRIVER_SEAT_OFFSET_X,
            Const::DRIVER_SEAT_OFFSET_Y,
            Const::DRIVER_SEAT_OFFSET_Z
        };
        draw_pos = trike.position + glm::vec3(
            c * seat_local.x - s * seat_local.z,
            seat_local.y,
            s * seat_local.x + c * seat_local.z);
        draw_yaw = trike.heading;
    } else {
        draw_pos = player.pos;
        draw_yaw = player.yaw;
    }

    // foot anchor correction
    float foot_lift = -d.model_foot_z * d.model_scale;

    float bob = 0.0f;
    if (player.mode == PLAYER_FOOT && player.speed > 0.1f)
        bob = std::sin(player.anim_timer * 2.0f) * 0.03f;

    float lean = 0.0f;
    if (player.mode == PLAYER_FOOT)
        lean = glm::clamp(player.speed / 4.0f, 0.0f, 1.0f) * glm::radians(8.0f);

    // model-space pivot is at feet (minY)
    glm::mat4 model = glm::mat4(1.0f);

    // 1. place feet at world position
    model = glm::translate(model, draw_pos + glm::vec3(0.0f, foot_lift + bob, 0.0f));

    // 2. face the right direction
    model = glm::rotate(model, -draw_yaw + glm::radians(90.0f), glm::vec3(0,1,0));

    // 3. trike roll/pitch inheritance — applied in world space before model corrections
    if (player.mode == PLAYER_DRIVING || player.mode == PLAYER_MOUNTING){
        glm::vec3 fwd   = glm::vec3(std::cos(trike.heading), 0.0f, std::sin(trike.heading));
        glm::vec3 right = glm::vec3(-std::sin(trike.heading), 0.0f, std::cos(trike.heading));
        model = glm::rotate(model, -trike.roll_angle,  fwd);
        model = glm::rotate(model, trike.pitch_angle, right);
    }

    // 4. model-space corrections: OBJ is Z-up, we need Y-up
    model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1,0,0));

    // 5. walking lean (in model space, tips forward along model Z after correction)
    if (player.mode == PLAYER_FOOT)
        model = glm::rotate(model, lean, glm::vec3(0,0,1));

    // 6. center XZ and scale
    glm::vec3 center_offset = glm::vec3(d.model_center.x, d.model_foot_z, 0.0f);
    model = model
        * glm::translate(glm::mat4(1.0f), -center_offset * d.model_scale)
        * glm::scale(glm::mat4(1.0f), glm::vec3(d.model_scale));

    glm::mat3 normal_mat = glm::mat3(glm::transpose(glm::inverse(model)));

    set_mat4(shader, "u_view", view);
    set_mat4(shader, "u_proj", proj);
    set_mat4(shader, "u_model", model);
    set_mat3(shader, "u_normal_mat", normal_mat);
    glUniform1i(glGetUniformLocation(shader.id, "u_use_checker"), 0);

    glBindVertexArray(d.mesh.vao);
    for (const auto& part : d.mesh.data.parts){
        for (const auto& grp : part.groups){
            if (grp.vertex_count <= 0) continue;
            const ObjMaterial* mat = obj_find_material(d.mesh.data, grp.mat_name);
            glm::vec3 kd = mat ? mat->kd : glm::vec3(0.6f, 0.4f, 0.25f);
            set_vec3(shader, "u_kd", kd);
            glDrawArrays(GL_TRIANGLES, grp.vertex_start, grp.vertex_count);
        }
    }
    glBindVertexArray(0);
}

void driver_model_destroy(DriverModel& d){
    obj_mesh_destroy(d.mesh);
}