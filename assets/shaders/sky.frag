// skybox drawn via a fullscreen triangle pair
// reconstructs view dir per-pixel, blends day/night/rain panoramas by time of day

#version 330 core
in vec2 v_ndc;
out vec4 frag_color;
uniform mat4      u_inv_view_proj;
uniform sampler2D u_sky_tex;       // day tex
uniform sampler2D u_sky_night_tex; // night tex
uniform sampler2D u_sky_rain_tex;  // rain tex
uniform vec3      u_tint_a;
uniform vec3      u_tint_b;
uniform int       u_flip_a;
uniform int       u_flip_b;
uniform int       u_use_night_b;   // 1 = sample night tex for B side
uniform float     u_blend;
uniform float     u_uv_offset;
uniform float     u_rain_blend;
uniform float     u_night_factor;
const float PI = 3.14159265;
void main(){
    vec4 world = u_inv_view_proj * vec4(v_ndc, 1.0, 1.0);
    vec3 dir = normalize(world.xyz / world.w);
    float base_u = (atan(dir.z, dir.x) / (2.0 * PI)) + 0.5 + u_uv_offset;
    float v = 1.0 - (asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5);

    float u_a = (u_flip_a == 1) ? (1.0 - base_u) : base_u;
    float u_b = (u_flip_b == 1) ? (1.0 - base_u) : base_u;

    vec3 col_a = texture(u_sky_tex, vec2(u_a, v)).rgb * u_tint_a;
    vec3 col_b = (u_use_night_b == 1)
        ? texture(u_sky_night_tex, vec2(u_b, v)).rgb * u_tint_b
        : texture(u_sky_tex, vec2(u_b, v)).rgb * u_tint_b;

    vec3 day_night = mix(col_a, col_b, u_blend);
    vec3 rain_sample = texture(u_sky_rain_tex, vec2(base_u, v)).rgb;
    // day/golden: mild desaturate + slight cool tint, keep texture readable
    // night: full greyscale conversion + heavy dim
    float lum = dot(rain_sample, vec3(0.299, 0.587, 0.114));
    vec3 rain_grey = vec3(lum) * vec3(0.58, 0.63, 0.70);
    rain_sample = mix(rain_sample, rain_grey, mix(0.25, 1.0, u_night_factor));
    rain_sample *= mix(0.88, 0.15, u_night_factor);
    frag_color = vec4(mix(day_night, rain_sample, u_rain_blend), 1.0);
}