#version 330 core
out vec4 frag_color;
uniform float u_alpha;
uniform vec2  u_res;
void main(){
    vec2 uv = (gl_FragCoord.xy / u_res) * 2.0 - 1.0;
    float vignette = 1.0 - smoothstep(0.3, 1.4, length(uv));
    frag_color = vec4(0.90, 0.95, 1.0, u_alpha * (0.6 + 0.4 * vignette));
}