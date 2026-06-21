#version 330 core
out vec4 frag_color;
uniform float u_alpha;
void main(){
    frag_color = vec4(0.82, 0.92, 1.00, u_alpha);
}