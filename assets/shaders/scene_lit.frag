// the original ground plane & proc mesh trike shader

#version 330 core
in vec3 v_world_normal;
in vec3 v_world_pos;
in vec4 v_light_space_pos;
out vec4 frag_color;
uniform vec3      u_kd;
uniform vec3      u_kd_alt;
uniform vec3      u_light_dir;
uniform vec3      u_light_color;
uniform float     u_checker_scale;
uniform int       u_use_checker;
uniform float     u_ambient;
uniform float     u_diff_intensity;
uniform sampler2D u_shadow_map;
uniform float     u_shadow_bias;
uniform vec3      u_fog_color;
uniform float     u_fog_near;
uniform float     u_fog_far;
uniform vec3      u_cam_pos_fog;

#define MAX_LIGHTS 150
uniform int   u_light_count;
uniform vec3  u_light_pos[MAX_LIGHTS];
uniform vec3  u_light_color_pt[MAX_LIGHTS];
uniform float u_light_radius[MAX_LIGHTS];
uniform float u_light_intensity[MAX_LIGHTS];

float shadow_pcf(vec4 lsp, vec3 normal, vec3 ldir){
    vec3 proj = lsp.xyz / lsp.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    float bias = max(u_shadow_bias * (1.0 - dot(normal, ldir)), u_shadow_bias * 0.1);
    float shadow = 0.0;
    vec2 texel = 1.0 / textureSize(u_shadow_map, 0);
    for (int x = -1; x <= 1; x++){
        for (int y = -1; y <= 1; y++){
            float pcf_depth = texture(u_shadow_map, proj.xy + vec2(x,y) * texel).r;
            shadow += (proj.z - bias > pcf_depth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main(){
    vec3 n = normalize(v_world_normal);
    vec3 ldir = normalize(u_light_dir);
    float diff = max(dot(n, ldir), 0.0);
    float shadow = shadow_pcf(v_light_space_pos, n, ldir);
    vec3 kd = u_kd;
    if (u_use_checker == 1){
        vec2 tile = floor(v_world_pos.xz * u_checker_scale);
        float parity = mod(tile.x + tile.y, 2.0);
        kd = mix(u_kd, u_kd_alt, parity);
    }
    vec3 lit = kd * u_light_color * (u_ambient + diff * u_diff_intensity * (1.0 - shadow));

    // point lights accumulated on top of sun pass
    for (int i = 0; i < u_light_count; i++){
        float dist = length(u_light_pos[i] - v_world_pos);
        if (dist >= u_light_radius[i]) continue;
        float atten = 1.0 - (dist / u_light_radius[i]);
        atten *= atten; // quadratic falloff
        vec3 l = normalize(u_light_pos[i] - v_world_pos);
        float ndotl = max(dot(n, l), 0.0);
        lit += kd * u_light_color_pt[i] * ndotl * atten * u_light_intensity[i];
    }

    float fog_dist = length(u_cam_pos_fog - v_world_pos);
    float fog_t = clamp((fog_dist - u_fog_near) / (u_fog_far - u_fog_near), 0.0, 1.0);
    fog_t = fog_t * fog_t;
    frag_color = vec4(mix(lit, u_fog_color, fog_t), 1.0);
}