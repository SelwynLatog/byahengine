#include "settings.hpp"
#include "const.hpp"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Settings my_settings = {};

static const char* SETTINGS_PATH = "../assets/settings.cfg";

static void apply_low() {
    my_settings.preset                = LOW;
    my_settings.render_shadows        = false;
    my_settings.shadow_map_size       = 512;
    my_settings.shadow_throttle_frame = 6;
    my_settings.prop_cull_dist        = 80.0f;
    my_settings.npc_cull_dist         = 20.0f;
    my_settings.light_cull_dist       = 60.0f;
    my_settings.rain_particle_count   = 1000;
    my_settings.rain_splash_max       = 0;
    my_settings.show_hud              = true;
    my_settings.render_fog            = false;
}

static void apply_moderate() {
    my_settings.preset                = MODERATE;
    my_settings.render_shadows        = true;
    my_settings.shadow_map_size       = 1024;
    my_settings.shadow_throttle_frame = 3;
    my_settings.prop_cull_dist        = 120.0f;
    my_settings.npc_cull_dist         = 30.0f;
    my_settings.light_cull_dist       = 90.0f;
    my_settings.rain_particle_count   = 2500;
    my_settings.rain_splash_max       = 300;
    my_settings.show_hud              = true;
    my_settings.render_fog            = true;
}

static void apply_high() {
    my_settings.preset                = HIGH;
    my_settings.render_shadows        = true;
    my_settings.shadow_map_size       = Const::SHADOW_MAP_SIZE;
    my_settings.shadow_throttle_frame = 1;
    my_settings.prop_cull_dist        = Const::PROP_CULL_DIST;
    my_settings.npc_cull_dist         = Const::NPC_CULL_DIST;
    my_settings.light_cull_dist       = Const::LIGHT_CULL_DIST;
    my_settings.rain_particle_count   = Const::RAIN_PARTICLE_COUNT;
    my_settings.rain_splash_max       = Const::RAIN_SPLASH_MAX;
    my_settings.show_hud              = true;
    my_settings.render_fog            = true;
}

void settings_apply_preset(Preset preset) {
    switch (preset) {
        case LOW: apply_low(); break;
        case MODERATE: apply_moderate(); break;
        case HIGH: apply_high(); break;
        case CUSTOM: my_settings.preset = CUSTOM;
            break;
    }
}

const char* settings_preset_name(Preset preset) {
    switch (preset) {
        case LOW: return "LOW";
        case MODERATE: return "MODERATE";
        case HIGH: return "HIGH";
        case CUSTOM: return "CUSTOM";
    }
    return "HIGH";
}

void settings_save() {
    FILE* f = fopen(SETTINGS_PATH, "w");
    if (!f) return;

    fprintf(f, "preset=%s\n",          settings_preset_name(my_settings.preset));
    fprintf(f, "render_shadows=%d\n",  (int)my_settings.render_shadows);
    fprintf(f, "shadow_map_size=%d\n", my_settings.shadow_map_size);
    fprintf(f, "shadow_throttle=%d\n", my_settings.shadow_throttle_frame);
    fprintf(f, "prop_cull=%.1f\n",     my_settings.prop_cull_dist);
    fprintf(f, "npc_cull=%.1f\n",      my_settings.npc_cull_dist);
    fprintf(f, "light_cull=%.1f\n",    my_settings.light_cull_dist);
    fprintf(f, "rain_particles=%d\n",  my_settings.rain_particle_count);
    fprintf(f, "rain_splashes=%d\n",   my_settings.rain_splash_max);
    fprintf(f, "show_hud=%d\n",        (int)my_settings.show_hud);
    fprintf(f, "render_fog=%d\n",     (int)my_settings.render_fog);

    fclose(f);
}

void settings_load() {
    apply_high(); // default if file missing or corrupt

    FILE* f = fopen(SETTINGS_PATH, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[64];
        if (sscanf(line, "%63[^=]=%63s", key, val) != 2) continue;

        if (strcmp(key, "preset") == 0) {
            if  (strcmp(val, "LOW") == 0) my_settings.preset = LOW;
            else if (strcmp(val, "MODERATE") == 0) my_settings.preset = MODERATE;
            else if (strcmp(val, "CUSTOM") == 0) my_settings.preset = CUSTOM;
            else my_settings.preset = HIGH;
        }
        else if (strcmp(key, "render_shadows") == 0) my_settings.render_shadows = atoi(val);
        else if (strcmp(key, "shadow_map_size") == 0) my_settings.shadow_map_size = atoi(val);
        else if (strcmp(key, "shadow_throttle") == 0) my_settings.shadow_throttle_frame = atoi(val);
        else if (strcmp(key, "prop_cull") == 0) my_settings.prop_cull_dist = (float)atof(val);
        else if (strcmp(key, "npc_cull") == 0) my_settings.npc_cull_dist = (float)atof(val);
        else if (strcmp(key, "light_cull") == 0) my_settings.light_cull_dist = (float)atof(val);
        else if (strcmp(key, "rain_particles") == 0) my_settings.rain_particle_count = atoi(val);
        else if (strcmp(key, "rain_splashes") == 0) my_settings.rain_splash_max = atoi(val);
        else if (strcmp(key, "show_hud") == 0) my_settings.show_hud = atoi(val);
        else if (strcmp(key, "render_fog") == 0) my_settings.render_fog = atoi(val);
    }

    fclose(f);
}