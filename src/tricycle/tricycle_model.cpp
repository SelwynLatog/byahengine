#include "tricycle_model.hpp"
#include "../core/const.hpp"
#include "../renderer/obj_loader.hpp"
#include "../renderer/shader.hpp"
#include <glm/glm.hpp>
#include <iostream>

void trike_model_init(TrikeModel& t) {
    ObjData data;
    if (!obj_load(Const::TRIKE_MODEL_PATH, data))
        std::cerr << "[trike_model] failed to load model\n";
    obj_mesh_init(t.mesh, std::move(data));
}

void trike_model_draw(const TrikeModel& t, const Shader& shader) {
    for (int i = 0; i < (int)t.mesh.data.groups.size(); i++) {
        const ObjGroup& grp = t.mesh.data.groups[i];
        const ObjMaterial* mat = obj_find_material(t.mesh.data, grp.mat_name);
        glm::vec3 kd = mat ? mat->kd : glm::vec3(0.8f);
        glUniform3f(glGetUniformLocation(shader.id, "u_kd"), kd.x, kd.y, kd.z);
        obj_mesh_draw_group(t.mesh, i);
    }
}

void trike_model_destroy(TrikeModel& t) {
    obj_mesh_destroy(t.mesh);
}