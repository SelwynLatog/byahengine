#version 330 core
in float v_alpha;
in float v_edge_fade;
out vec4 frag_color;
uniform float u_alpha;
void main(){
    frag_color = vec4(0.78, 0.88, 1.00, u_alpha * v_alpha * v_edge_fade);
}