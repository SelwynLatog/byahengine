#version 330 core
const vec2 VERTS[6] = vec2[](
    vec2(-1,-1), vec2(1,-1), vec2(1,1),
    vec2(-1,-1), vec2(1,1), vec2(-1,1)
);
void main(){
    gl_Position = vec4(VERTS[gl_VertexID], 0.0, 1.0);
}