/*
 * RAIN SYSTEM 
 * billboard quad particles n pos update every frame
 * vertex shader expands each pos into a cam facing quad
 * angled by wind dir to get that streak look
 */
 
#version 330 core
    
layout(location = 0) in vec3  a_pos;
layout(location = 1) in float a_len_scale;
layout(location = 2) in float a_width_scale;
layout(location = 3) in float a_alpha_scale;

uniform mat4  u_view;
uniform mat4  u_proj;
uniform vec3  u_cam_right;
uniform vec3  u_streak_dir;
uniform float u_streak_len;
uniform float u_streak_width;
uniform vec3  u_cam_center;
uniform float u_box_half_xz;
    
out float v_alpha;
out float v_edge_fade;

void main(){
    int qi = gl_VertexID % 6;
    
    float dx = abs(a_pos.x - u_cam_center.x) / u_box_half_xz;
    float dz = abs(a_pos.z - u_cam_center.z) / u_box_half_xz;
    float edge = max(dx, dz);
    v_edge_fade = 1.0 - smoothstep(0.65, 1.0, edge);

    vec3 top = a_pos;
    vec3 bot = a_pos + u_streak_dir * (u_streak_len * a_len_scale);
    vec3 right = u_cam_right * (u_streak_width * a_width_scale);
    v_alpha = a_alpha_scale;
    
    vec3 TL = top - right;
    vec3 TR = top + right;
    vec3 BL = bot - right;
    vec3 BR = bot + right;
    
    vec3 p;
    if (qi == 0) p = TL;
    else if (qi == 1) p = BL;
    else if (qi == 2) p = TR;
    else if (qi == 3) p = BL;
    else if (qi == 4) p = BR;
    else p = TR;
    
    gl_Position = u_proj * u_view * vec4(p, 1.0);
}