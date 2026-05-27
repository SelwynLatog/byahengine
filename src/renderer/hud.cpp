#include "hud.hpp"
#include <glad/glad.h>
#include "../../src/core/const.hpp"
#include <glm/glm.hpp>
#include <cstdio>
#include <cstdarg>
#include <string>

void hud_init(Hud& h, int window_width, int window_height){
    font_init(h.font, window_width, window_height);
}

void hud_destroy(Hud& h){
    font_destroy(h.font);
}

// helpers

// convert heading radians to compass string
static const char* heading_to_compass(float heading_rad){
    float deg = glm::degrees(heading_rad);
    if (deg < 0) deg += 360.0f;
    if (deg < 22.5f || deg >= 337.5f) return "E";
    if (deg < 67.5f) return "NE";
    if (deg < 112.5f) return "N";
    if (deg < 157.5f) return "NW";
    if (deg < 202.5f) return "W";
    if (deg < 247.5f) return "SW";
    if (deg < 292.5f) return "S";
    return "SE";
}

static std::string fmt(const char* format, ...){
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return std::string(buf);
}

// hud_draw
void hud_draw(const Hud& h, const TrikeState& trike){
    const int SCALE= 2;   // 16x16px glyphs
    const int LEFT= 20;  // left margin px
    const int TOP= 20;  // top margin px
    const int LINE_H= 20;  // px between lines

    // speed in km/h
    float kmh = trike.speed * 3.6f;

    // engine state string
    const char* state_str = "COASTING";
    float sr = 0.7f, sg = 0.7f, sb = 0.7f; // default grey
    if (trike.speed > 0.1f){
        state_str = "THROTTLE";
        sr = 0.2f; sg = 1.0f; sb = 0.2f; // green
    } 
    else if (trike.speed < -0.1f){
        state_str = "BRAKING";
        sr = 1.0f; sg = 0.3f; sb = 0.2f; // red
    }

    // steer direction string
    float steer_deg = glm::degrees(trike.steer_angle);
    std::string steer_str;
    if (steer_deg >  1.0f) steer_str = fmt("%.1f R", steer_deg);
    else if (steer_deg < -1.0f) steer_str = fmt("%.1f L", -steer_deg);
    else steer_str = "STRAIGHT";

    // heading
    float hdg_deg = glm::degrees(trike.heading);
    if (hdg_deg < 0) hdg_deg += 360.0f;

    int line = 0;
    auto draw_line = [&](const std::string& text, float r, float g, float b){
        font_draw(h.font, text, LEFT, TOP + line * LINE_H, SCALE, r, g, b);
        line++;
    };

    // white for labels, yellow for values
    draw_line(fmt("SPEED %.1f km/h", kmh), 1.0f, 1.0f, 0.2f);
    draw_line(fmt("POS X:%.1f  Z:%.1f", trike.position.x, trike.position.z), 1.0f, 1.0f, 1.0f);

     // compass accounts for model yaw offset so it shows visual facing direction
    float visual_heading= trike.heading + glm::radians(Const::TRIKE_MODEL_YAW_OFFSET);
    float visual_deg= glm::degrees(visual_heading);
    if (visual_deg < 0) visual_deg += 360.0f;
    if (visual_deg >= 360.0f) visual_deg -= 360.0f;

    draw_line(fmt("HEADING %s  (%.0f deg)",
                  heading_to_compass(trike.heading), hdg_deg), 1.0f, 1.0f, 1.0f);
    draw_line(fmt("STEER %s", steer_str.c_str()), 1.0f, 1.0f, 1.0f);
    draw_line(fmt("STATE %s", state_str), sr, sg, sb);

    // impact readout
    // fades with timer so it naturally disappears
    if (trike.impact_timer > 0.0f ){
        float fade= glm::clamp(trike.impact_timer / 0.35f, 0.0f, 1.0f);
        draw_line(fmt("IMPACT %.f N", trike.last_impact_force * Const::TRIKE_MASS), 1.0F, 0.2f * fade, 0.2f * fade);
    }
}