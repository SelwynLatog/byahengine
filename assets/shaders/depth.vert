// shadow pass
// Renders every shadow-casting mesh from the sun's pov into the depth-only framebuffer
// which every other lit shader then samples via shadow_pcf to know what's in shadow


#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_light_space;
uniform mat4 u_model;
void main(){
    gl_Position = u_light_space * u_model * vec4(a_pos, 1.0);
}