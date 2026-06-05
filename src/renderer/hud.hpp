#pragma once
#include "font.hpp"
#include "../physics/trike_state.hpp"
struct Hud {
    Font font;
};

void hud_init(Hud& h, int window_width, int window_height);
void hud_draw(const Hud& h, const TrikeState& trike, bool has_passenger = false, float fare = 0.0f);
void hud_draw_direction_arrow(const Hud& h, float dot, float cross_y, float dist);
void hud_destroy(Hud& h);