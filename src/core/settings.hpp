#pragma once
// runtime const settings
// const.hpp values are the compile-time ceiling/defaults
// my_settings is what the engine actually reads at runtime

enum Preset {
    LOW,
    MODERATE,
    HIGH,
    CUSTOM
};

// every val is self explanatory in const.hpp
struct Settings{
    Preset preset;

    bool render_shadows;
    int shadow_map_size;
    int shadow_throttle_frame;

    float prop_cull_dist;
    float npc_cull_dist;
    float light_cull_dist;

    int rain_particle_count;
    int rain_splash_max;

    bool show_hud;
};

extern Settings my_settings;

void settings_load();
void settings_save();
void settings_apply_preset(Preset preset);
const char* settings_preset_name(Preset preset);