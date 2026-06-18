#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;

uniform mat4  u_view;
uniform mat4  u_proj;
uniform float u_time;
uniform float u_y_level;

out vec3 v_world_pos;
out vec3 v_normal;

const float PI = 3.14159265;

vec3 gerstner(vec2 xz, vec2 dir, float wavelength, float amplitude, float speed) {
    float k     = 2.0 * PI / wavelength;
    float w     = sqrt(9.8 * k);
    float phase = dot(dir, xz) * k - w * u_time * speed;
    float Q     = 0.5;
    return vec3(
        dir.x * Q * amplitude * cos(phase),
        amplitude * sin(phase),
        dir.y * Q * amplitude * cos(phase)
    );
}

vec3 gerstner_normal(vec2 xz, vec2 dir, float wavelength, float amplitude, float speed) {
    float k     = 2.0 * PI / wavelength;
    float w     = sqrt(9.8 * k);
    float phase = dot(dir, xz) * k - w * u_time * speed;
    float Q     = 0.5;
    float wa    = k * amplitude;
    return vec3(
        -dir.x * wa * cos(phase),
        1.0 - Q * wa * sin(phase),
        -dir.y * wa * cos(phase)
    );
}

void main() {
    vec2 xz = a_pos.xz;

    vec3 d = vec3(0.0);
    d += gerstner(xz, normalize(vec2( 1.0,  0.6)), 18.0, 0.18, 1.0);
    d += gerstner(xz, normalize(vec2(-0.7,  1.0)), 11.0, 0.10, 1.2);
    d += gerstner(xz, normalize(vec2( 0.3, -1.0)),  7.0, 0.06, 0.9);
    d += gerstner(xz, normalize(vec2(-1.0, -0.4)),  5.0, 0.04, 1.4);

    vec3 n = vec3(0.0);
    n += gerstner_normal(xz, normalize(vec2( 1.0,  0.6)), 18.0, 0.18, 1.0);
    n += gerstner_normal(xz, normalize(vec2(-0.7,  1.0)), 11.0, 0.10, 1.2);
    n += gerstner_normal(xz, normalize(vec2( 0.3, -1.0)),  7.0, 0.06, 0.9);
    n += gerstner_normal(xz, normalize(vec2(-1.0, -0.4)),  5.0, 0.04, 1.4);

    vec3 world  = a_pos + d;
    world.y    += u_y_level - a_pos.y;

    v_world_pos = world;
    v_normal    = normalize(n);

    gl_Position = u_proj * u_view * vec4(world, 1.0);
}