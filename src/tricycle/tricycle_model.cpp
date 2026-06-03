#include "tricycle_model.hpp"
#include "../core/const.hpp"
#include "../renderer/obj_loader.hpp"
#include "../renderer/obj_mesh.hpp"
#include "../renderer/shader.hpp"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

static void set_uniform_vec3(const Shader& s, const char* n, glm::vec3 v){
    glUniform3f(glGetUniformLocation(s.id, n), v.x, v.y, v.z);
}
static void set_uniform_mat4(const Shader& s, const char* n, const glm::mat4& m){
    glUniformMatrix4fv(glGetUniformLocation(s.id, n), 1, GL_FALSE, glm::value_ptr(m));
}
static void set_uniform_mat3(const Shader& s, const char* n, const glm::mat3& m){
    glUniformMatrix3fv(glGetUniformLocation(s.id, n), 1, GL_FALSE, glm::value_ptr(m));
}

// draws one named part with the given model matrix
// sets u_model, u_normal_mat, u_kd per material group
static void draw_part(
    const ObjMesh& mesh,
    const std::string& part_name,
    const glm::mat4& model,
    const Shader& shader)
{
    const ObjPart* part = obj_find_part(mesh.data, part_name);
    if (!part) return; // part name not in OBJ — silent skip

    glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(model)));
    set_uniform_mat4(shader, "u_model", model);
    set_uniform_mat3(shader, "u_normal_mat", nm);

    // per-group draw: each material group gets its own kd uniform
    glBindVertexArray(mesh.vao);
    for (const auto& grp : part->groups){
        if (grp.vertex_count <= 0) continue;
        const ObjMaterial* mat = obj_find_material(mesh.data, grp.mat_name);
        glm::vec3 kd = mat ? mat->kd : glm::vec3(0.8f);
        set_uniform_vec3(shader, "u_kd", kd);
        glDrawArrays(GL_TRIANGLES, grp.vertex_start, grp.vertex_count);
    }
    glBindVertexArray(0);
}

void trike_model_init(TrikeModel& t){
    ObjData data;
    if (!obj_load(Const::TRIKE_PARTS_MODEL_PATH, data))
        std::cerr << "[trike_model] failed to load parts model\n";
    obj_mesh_init(t.mesh, std::move(data));
    t.wheel_spin = 0.0f;
}

void trike_model_update(TrikeModel& t, float speed, float dt){
    // accumulate spin: arc length per frame / radius = angle per frame
    t.wheel_spin += (speed / Const::TRIKE_WHEEL_RADIUS) * dt;
}

void trike_model_draw(
    const TrikeModel& t,
    const TrikeState& trike,
    const Shader& shader,
    const glm::mat4& view,
    const glm::mat4& proj)
{
    set_uniform_mat4(shader, "u_view", view);
    set_uniform_mat4(shader, "u_proj", proj);

    // body matrix
    // shared base for all parts: world position + heading + roll + model offset
    // model_center and model_scale are baked into the mesh at load time
    // we compute a rough bounding box center from the full vertex buffer

    // compute model bounds once at draw time from full vertex buffer
    // this is cheap enough — only runs when drawing, not every physics tick
    float minX= 1e9f, maxX=-1e9f;
    float minY= 1e9f, maxY=-1e9f;
    float minZ= 1e9f, maxZ=-1e9f;
    for (int i = 0; i < (int)t.mesh.data.vertices.size(); i += 8){
        float x = t.mesh.data.vertices[i];
        float y = t.mesh.data.vertices[i+1];
        float z = t.mesh.data.vertices[i+2];
        minX=std::min(minX,x); maxX=std::max(maxX,x);
        minY=std::min(minY,y); maxY=std::max(maxY,y);
        minZ=std::min(minZ,z); maxZ=std::max(maxZ,z);
    }
    float longest = std::max(maxX-minX, std::max(maxY-minY, maxZ-minZ));
    float model_scale = (longest > 0.0f) ? Const::MODEL_NORMALIZE_SIZE / longest : 1.0f;
    glm::vec3 model_center = glm::vec3(
        (minX+maxX)*0.5f,
         minY,
        (minZ+maxZ)*0.5f);
    model_center.y -= Const::MODEL_FLOOR_FUDGE;
    glm::vec3 sc = model_center * model_scale;

    // render position
    glm::vec3 render_pos = trike.position;
    render_pos.y += trike.body_bob;
    float model_half_height = (maxY - minY) * 0.5f * model_scale;
    if (trike.is_tipping)
        render_pos.y = model_half_height * std::abs(std::cos(trike.roll_angle))
                     * std::abs(std::cos(trike.tumble_pitch));
    else if (trike.is_rolled_over)
        render_pos.y = 0.0f;
    

    // heading-relative axes for correct tumble rotation
    glm::vec3 fwd_ws   = glm::vec3( std::cos(trike.heading), 0.0f, std::sin(trike.heading));
    glm::vec3 right_ws = glm::vec3(-std::sin(trike.heading), 0.0f, std::cos(trike.heading));

    // during tumble use independent pitch+roll accumulators
    // during normal driving use physics roll + terrain pitch
    float render_roll  = trike.is_tipping ? trike.roll_angle   : trike.roll_angle;
    float render_pitch = trike.is_tipping ? trike.tumble_pitch : trike.pitch_angle;

    // world_base: shared by all parts 
    // roll around heading axis, pitch around right axis
    glm::mat4 world_base =
        glm::translate(glm::mat4(1.0f), render_pos)
        * glm::rotate(glm::mat4(1.0f), -trike.heading, glm::vec3(0,1,0))
        * glm::rotate(glm::mat4(1.0f), -render_roll, fwd_ws)
        * glm::rotate(glm::mat4(1.0f), render_pitch, right_ws)
        * glm::rotate(glm::mat4(1.0f), glm::radians(Const::TRIKE_MODEL_YAW_OFFSET), glm::vec3(0,1,0))
        * glm::rotate(glm::mat4(1.0f), trike.shake_pitch, glm::vec3(1,0,0))
        * glm::rotate(glm::mat4(1.0f), trike.shake_roll,  glm::vec3(0,0,1));

    // body: world_base + model-space centering + scale
    glm::mat4 body = world_base
        * glm::translate(glm::mat4(1.0f), -sc)
        * glm::scale(glm::mat4(1.0f), glm::vec3(model_scale));

    // static body parts 
    // cab, cart, rear spring, driver
    draw_part(t.mesh, "trike_cab_body", body, shader);
    draw_part(t.mesh, "trike_driver_body", body, shader);
    draw_part(t.mesh, "cart", body, shader);
    draw_part(t.mesh, "trike_rear_spring", body, shader);

    // spinning wheels
    // spin axis is local X (axle runs left-right in model space)
    // pivot_offset from obj_loader is the geometric center of the part in model space
    auto wheel_matrix = [&](const std::string& part_name, float extra_yaw) -> glm::mat4 {
        const ObjPart* part = obj_find_part(t.mesh.data, part_name);
        glm::vec3 pivot = part ? part->pivot_offset : glm::vec3(0.0f);

        // pivot is in raw model space. we spin around it before the body transform
        // closes over scale + heading + position
        // this prevents cords mismatch
        // body = T(world) * R(heading) * R(roll) * R(yaw_offset) * T(-sc) * S(scale)
        // we insert the spin just before T(-sc)*S(scale), in raw model space.
        glm::vec3 p = pivot * model_scale - sc; // pivot in the same space body outputs

        return world_base
            * glm::translate(glm::mat4(1.0f),  p)
            * glm::rotate(glm::mat4(1.0f), extra_yaw, glm::vec3(0,1,0))
            * glm::rotate(glm::mat4(1.0f), -t.wheel_spin, glm::vec3(0,0,1))
            * glm::translate(glm::mat4(1.0f), -p)
            * glm::translate(glm::mat4(1.0f), -sc)
            * glm::scale(glm::mat4(1.0f), glm::vec3(model_scale));
    };


    draw_part(t.mesh, "trike_rear_wheel", wheel_matrix("trike_rear_wheel", 0.0f), shader);
    draw_part(t.mesh, "trike_sidecar_wheel", wheel_matrix("trike_sidecar_wheel", 0.0f), shader);
    draw_part(t.mesh, "trike_front_wheel", wheel_matrix("trike_front_wheel", -trike.steer_angle), shader);

    // steered parts 
    // fork and handlebar rotate about the steering axis (local Y through their pivot)

    auto steer_matrix = [&](const std::string& part_name) -> glm::mat4 {
        const ObjPart* part = obj_find_part(t.mesh.data, part_name);
        glm::vec3 pivot = part ? part->pivot_offset : glm::vec3(0.0f);
        glm::vec3 p = pivot * model_scale - sc;

        return world_base
            * glm::translate(glm::mat4(1.0f),  p)
            * glm::rotate(glm::mat4(1.0f), -trike.steer_angle, glm::vec3(0,1,0))
            * glm::translate(glm::mat4(1.0f), -p)
            * glm::translate(glm::mat4(1.0f), -sc)
            * glm::scale(glm::mat4(1.0f), glm::vec3(model_scale));
    };
    draw_part(t.mesh, "trike_front_fork", steer_matrix("trike_front_fork"), shader);
    draw_part(t.mesh, "trike_handlebar", steer_matrix("trike_handlebar"), shader);
}

void trike_model_destroy(TrikeModel& t){
    obj_mesh_destroy(t.mesh);
}