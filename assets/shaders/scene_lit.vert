#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat3 u_normal_mat;
uniform mat4 u_light_space;
out vec3 v_world_normal;
out vec3 v_world_pos;
out vec4 v_light_space_pos;
void main(){
    vec4 world = u_model * vec4(a_pos, 1.0);
    gl_Position = u_proj * u_view * world;
    v_world_normal = normalize(u_normal_mat * a_normal);
    v_world_pos = world.xyz;
    v_light_space_pos = u_light_space * world;
}