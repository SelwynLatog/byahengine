#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "../core/const.hpp"

struct ma_engine;
struct ma_sound;

struct AudioVoice {
    ma_sound* sound = nullptr;
    bool in_use = false;
};

struct AudioSystem {
    ma_engine* engine = nullptr; // heap alloc so header stays clean

    // trike engine layers 
    // looping, pitch shifted by speed
    ma_sound* eng_idle = nullptr;
    ma_sound* eng_mid  = nullptr;
    ma_sound* eng_high = nullptr;
    
    // ambient base loop
    ma_sound* ambient = nullptr;

    // radio
    ma_sound* radio = nullptr;
    std::vector<std::string> radio_playlist;
    int radio_index  = 0;
    float radio_volume = 0.8f;
    bool radio_on = false;

    // ring buffer alloc
    AudioVoice impact_pool[Const::AUDIO_IMPACT_VOICES];
    int impact_head = 0;

    AudioVoice voice_pool[Const::AUDIO_VOICE_VOICES];
    int voice_head = 0;

    AudioVoice step_pool[Const::AUDIO_STEP_VOICES];
    int step_head = 0;

    // 3D listener state
    glm::vec3 listener_pos = glm::vec3(0.0f);
    glm::vec3 listener_fwd = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 listener_up  = glm::vec3(0.0f, 1.0f, 0.0f);

    // footstep trigger
    float step_timer = 0.0f; // accumulates anim_timer delta
    float step_interval = 1.7f; // seconds between steps at walk speed

    bool initialized = false;

    // impact spam prevention
    // cull max audio to prevent multi layered audio triggers 
    // especially on grouped objects
    float impact_cooldown = 0.0f;
    static constexpr float IMPACT_COOLDOWN_INTERVAL = 0.18f; // seconds

    // env ambience slots
    // audio system needs submode for area based selection
    struct AmbienceSlot{
        ma_sound* sound = nullptr;
        int zone_id = -1;
        float cur_vol = 0.0f; // for faded volume
        bool playing = false;
    };
    AmbienceSlot ambience_slots[Const::MAX_AMBIENCE_SLOTS];

    // global for rain
    ma_sound* rain = nullptr;
    bool rain_active = false;
    float rain_vol = 0.0f;
};

// lifecycle
bool audio_init(AudioSystem& audio, const char* assets_dir);
void audio_shutdown(AudioSystem& audio);

// called every frame from app_run
// updates listener, engine layers, radio track end detection
void audio_update(AudioSystem& audio, float dt,
    const glm::vec3& listener_pos, const glm::vec3& listener_fwd,
    float trike_speed, float trike_max_speed, bool is_driving);

// one-shot triggers 
// called at the right state transitions in app.cpp
// pos = world position of the sound source for 3D attenuation
void audio_trigger_impact(AudioSystem& audio,
    const std::string& path, const glm::vec3& pos, float force);

void audio_trigger_voice(AudioSystem& audio,
    const std::string& path, const glm::vec3& pos);

// non-spatial variant for passenger voices 
// always full volume, no distance fade
void audio_trigger_voice_local(AudioSystem& audio, const std::string& path);

void audio_trigger_step(AudioSystem& audio,
    const std::string& path, float anim_timer_delta);

// radio controls
void audio_radio_next(AudioSystem& audio);
void audio_radio_toggle(AudioSystem& audio);
void audio_radio_set_volume(AudioSystem& audio, float vol);

struct AmbienceZone;
void audio_update_env(AudioSystem& audio, float dt, const glm::vec3& listener_pos,
    const AmbienceZone* zones, int zone_count, float night_factor);

void audio_rain_set(AudioSystem& audio, bool active);

void audio_pause(AudioSystem& audio);
void audio_resume(AudioSystem& audio);