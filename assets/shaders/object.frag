// prop/NPC/world-objet shader
// textured world objects 
// (Blender-exported OBJ props, NPCs, the trike and driver models when not using proc mesh)
// with point lights, sun, shadows, fog, and alpha-tested transparency


#version 330 core
in vec3 v_world_normal;
in vec3 v_world_pos;
in vec2 v_uv;
in vec4 v_light_space_pos;
out vec4 frag_color;
uniform vec3      u_kd;
uniform vec3      u_light_dir;
uniform sampler2D u_tex;
uniform sampler2D u_shadow_map;
uniform int       u_use_texture;
uniform float     u_shadow_bias;
uniform vec3      u_fog_color;
uniform float     u_fog_near;
uniform float     u_fog_far;
uniform vec3      u_cam_pos_fog;
uniform float     u_ambient;
uniform float     u_diff_intensity;
uniform vec3      u_light_color;

#define MAX_LIGHTS 150
uniform int   u_light_count;
uniform vec3  u_light_pos[MAX_LIGHTS];
uniform vec3  u_light_color_pt[MAX_LIGHTS];
uniform float u_light_radius[MAX_LIGHTS];
uniform float u_light_intensity[MAX_LIGHTS];
uniform vec3  u_light_spot_dir[MAX_LIGHTS];
uniform float u_light_cos_cutoff[MAX_LIGHTS];

float shadow_pcf(vec4 lsp, vec3 normal, vec3 ldir){
    vec3 proj = lsp.xyz / lsp.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    float current_depth = proj.z;
    float bias = max(u_shadow_bias * (1.0 - dot(normal, ldir)), u_shadow_bias * 0.1);
    float shadow = 0.0;
    vec2 texel = 1.0 / textureSize(u_shadow_map, 0);
    for (int x = -1; x <= 1; x++){
        for (int y = -1; y <= 1; y++){
            float pcf_depth = texture(u_shadow_map, proj.xy + vec2(x,y) * texel).r;
            shadow += (current_depth - bias > pcf_depth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main(){
    vec3 n = normalize(v_world_normal);
    vec3 ldir = normalize(u_light_dir);
    float diff = max(dot(n, ldir), 0.0);
    float ambient = u_ambient;
    vec4 tex_sample = (u_use_texture == 1) ? texture(u_tex, v_uv) : vec4(u_kd, 1.0);
    if (u_use_texture == 1 && tex_sample.a < 0.5) discard;
    float shadow = shadow_pcf(v_light_space_pos, n, ldir);
    vec3 lit = tex_sample.rgb * u_light_color * (ambient + diff * u_diff_intensity * (1.0 - shadow));

    for (int i = 0; i < u_light_count; i++){
        vec3 to_frag = v_world_pos - u_light_pos[i];
        float dist = length(to_frag);
        if (dist >= u_light_radius[i]) continue;
        vec3 l = normalize(-to_frag);
        float ndotl = max(dot(n, l), 0.0);

        // spotlight: add a cheap ambient fill for nearby geometry (trike body, driver)
        // this prevents the contrast darkening effect when ground is brightly lit
        if (u_light_cos_cutoff[i] > -1.0){
            float fill_atten = 1.0 - clamp(dist / 3.5f, 0.0, 1.0);
            fill_atten *= fill_atten;
            lit += tex_sample.rgb * u_light_color_pt[i] * 0.35 * fill_atten * u_light_intensity[i] * 0.15;
        }

        // omni point light path
        if (u_light_cos_cutoff[i] <= -1.0){
            float atten = 1.0 - (dist / u_light_radius[i]);
            atten *= atten;
            lit += tex_sample.rgb * u_light_color_pt[i] * ndotl * atten * u_light_intensity[i];
            continue;
        }

        // spotlight path
        vec3 spot = normalize(u_light_spot_dir[i]);
        // to_frag points light->fragment, spot points forward
        // only illuminate fragments in front of the light
        float axial = dot(to_frag, spot);
        if (axial <= 0.0) continue; // behind the light source, skip

        float cos_angle = dot(normalize(to_frag), spot);
        if (cos_angle < u_light_cos_cutoff[i]) continue;

        // axial attenuation
        // fades with distance along beam axis
        float axial_t = clamp(axial / u_light_radius[i], 0.0, 1.0);
        float axial_atten = 1.0 - axial_t;

        // cone edge softness
        float cone = smoothstep(u_light_cos_cutoff[i], u_light_cos_cutoff[i] + 0.18, cos_angle);

        float surface = max(dot(n, l), 0.0) * 0.4 + 0.6;

        lit += tex_sample.rgb * u_light_color_pt[i] * surface * axial_atten * u_light_intensity[i] * cone;

    }


    float fog_dist = length(u_cam_pos_fog - v_world_pos);
    float fog_t = clamp((fog_dist - u_fog_near) / (u_fog_far - u_fog_near), 0.0, 1.0);
    fog_t = fog_t * fog_t;
    frag_color = vec4(mix(lit, u_fog_color, fog_t), 1.0);
}