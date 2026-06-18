#version 330 core
layout(location = 0) in vec2 a_pos;
out vec2 v_ndc;
void main(){
    v_ndc = a_pos;
    gl_Position = vec4(a_pos, 0.9999, 1.0);
}