// Vertex stage stacks four Gerstner waves to displace the mesh and compute normals
// frag does fresnel based color mixing, specular highlight, sun directional sparkle
// via voronoi noise, screen space caustics, foam at wave crests, and horizon fade


#version 330 core
in vec3 v_world_pos;
in vec3 v_normal;
out vec4 frag_color;

uniform vec3  u_light_dir;
uniform vec3  u_cam_pos;
uniform float u_time;
uniform vec3  u_light_color;
uniform float u_ambient;
uniform float u_diff_intensity;

float hash(vec2 p){
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

float vnoise(vec2 p){
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    vec2 u  = fp * fp * (3.0 - 2.0 * fp);
    return mix(
        mix(hash(ip),           hash(ip+vec2(1,0)), u.x),
        mix(hash(ip+vec2(0,1)), hash(ip+vec2(1,1)), u.x),
        u.y);
}

float voronoi(vec2 p, float scale, float tscale){
    p *= scale;
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    float md = 1.0;
    for(int y=-1;y<=1;y++)
    for(int x=-1;x<=1;x++){
        vec2 nb  = vec2(float(x), float(y));
        vec2 off = vec2(
            hash(ip + nb + vec2(u_time * tscale, 0.0)),
            hash(ip + nb + vec2(0.0, u_time * tscale))
        );
        md = min(md, length(nb + off - fp));
    }
    return md;
}

void main(){
    vec3 view = normalize(u_cam_pos - v_world_pos);
    vec3 ldir = normalize(u_light_dir);
    vec2 xz = v_world_pos.xz;
    float dist  = length(u_cam_pos - v_world_pos);

    // Micro-normal
    vec2 s1 = xz * 0.14 + vec2( u_time * 0.11,  u_time * 0.07);
    vec2 s2 = xz * 0.29 + vec2(-u_time * 0.08,  u_time * 0.14);
    float n1 = vnoise(s1) * 2.0 - 1.0;
    float n2 = vnoise(s2) * 2.0 - 1.0;
    vec3 micro_n = normalize(v_normal + vec3(
        n1 * 0.08 + n2 * 0.04,
        0.0,
        n1 * 0.08 + n2 * 0.04
    ));

    // Fresnel
    float NdotV   = max(dot(micro_n, view), 0.0);
    float fresnel = mix(0.04, 1.0, pow(1.0 - NdotV, 5.0));

    // Base color
    vec3 col_shallow = vec3(0.08, 0.60, 0.72);
    vec3 col_deep = vec3(0.02, 0.20, 0.50);
    vec3 water_col = mix(col_deep, col_shallow, pow(NdotV, 0.6));

    // Sky reflection
    float h_t = pow(1.0 - NdotV, 3.0);
    vec3 sky_near = vec3(0.30, 0.65, 0.95);
    vec3 sky_horiz = vec3(0.76, 0.90, 1.00);
    vec3 sky_col = mix(sky_near, sky_horiz, h_t);
    water_col = mix(water_col, sky_col, fresnel * 0.52);

    // Light tint applied once after all color composition
    float diff = max(dot(micro_n, ldir), 0.0);
    float light = u_ambient + diff * u_diff_intensity * 0.08;
    light = max(light, 0.15);
    water_col = water_col * u_light_color * light;

    // Depth variation
    float wave_depth = smoothstep(-0.35, 0.35, v_world_pos.y);
    vec3 trough_tint = mix(water_col, col_deep,    0.07);
    vec3 crest_tint = mix(water_col, col_shallow, 0.05);
    water_col = mix(trough_tint, crest_tint, wave_depth);

    // Specular 
    vec3  hv   = normalize(ldir + view);
    float spec = pow(max(dot(micro_n, hv), 0.0), 220.0) * 1.6;
    water_col += vec3(1.00, 0.97, 0.88) * spec;

    // Sparkles
    vec3 sun_mirror = reflect(-ldir, vec3(0,1,0));
    vec2 lane_dir = normalize(sun_mirror.xz + vec2(0.001));
    vec2 to_frag = normalize(xz - u_cam_pos.xz + vec2(0.001));
    float lane_t = dot(to_frag, lane_dir);
    float lane_mask = smoothstep(-0.25, 0.85, lane_t);
    float dist_fade = 1.0 - smoothstep(10.0, 65.0, dist);

    float v1 = voronoi(xz + vec2( u_time*0.18,  u_time*0.12),  5.0, 0.11);
    float v2 = voronoi(xz + vec2(-u_time*0.14,  u_time*0.22),  9.0, 0.08);
    float v3 = voronoi(xz + vec2( u_time*0.07, -u_time*0.19), 16.0, 0.13);

    float sp1 = smoothstep(0.16, 0.0, v1);
    float sp2 = smoothstep(0.12, 0.0, v2);
    float sp3 = smoothstep(0.09, 0.0, v3);

    float sun_gate = max(dot(micro_n, ldir), 0.15);
    sun_gate = sun_gate * sun_gate;

    // sun_strength: how bright/warm the sun is — kills sparkles at dusk/night
    float sun_strength = clamp(dot(u_light_color, vec3(0.333)), 0.0, 1.0);
    float sparkle = (sp1*0.55 + sp2*0.80 + sp3*1.0)
                    * sun_gate * lane_mask * dist_fade * 3.5
                    * sun_strength;
    water_col += vec3(1.00, 0.98, 0.92) * sparkle;

    // Caustics
    vec2 cx1 = xz * 3.2 + vec2( u_time*0.50, u_time*0.35);
    vec2 cx2 = xz * 3.2 + vec2(-u_time*0.38, u_time*0.58);
    float caustic = smoothstep(0.60, 0.85, vnoise(cx1) * vnoise(cx2) * 4.0);
    float c_fade  = 1.0 - smoothstep(0.0, 22.0, dist);
    water_col += vec3(0.50, 0.84, 1.00) * caustic * c_fade * 0.28 * sun_strength; 
   

    // Horizon fade
    float h_fade = smoothstep(35.0, 170.0, dist);
    water_col = mix(water_col, sky_col * 1.05, h_fade * 0.38);

    // Foam
    float foam_n = vnoise(xz * 3.5 + u_time * 0.22) * 0.35 + 0.65;
    float foam = smoothstep(0.32, 0.50, v_world_pos.y + 0.22) * foam_n;
    water_col = mix(water_col, vec3(0.94, 0.97, 1.00), foam * 0.36);

    // Alpha
    float alpha = clamp(0.85 + fresnel * 0.12 + foam * 0.04, 0.83, 0.97);
    frag_color = vec4(water_col, alpha);
}