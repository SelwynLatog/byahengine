#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio.hpp"
#include "../world/ambience_zone.hpp"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <unordered_map>

// internal helpers
static ma_sound* alloc_sound(ma_engine* engine, const std::string& path,
    bool loop, bool spatial, float volume = 1.0f)
{
    if (path.empty() || !std::filesystem::exists(path)) return nullptr;
    ma_sound* s = new ma_sound{};
    ma_uint32 flags = spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file(engine, path.c_str(), flags, nullptr, nullptr, s) != MA_SUCCESS){
        delete s;
        std::cerr << "[audio] failed to load: " << path << "\n";
        return nullptr;
    }
    ma_sound_set_looping(s, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(s, volume);
    return s;
}

static void free_sound(ma_sound* s){
    if (!s) return;
    ma_sound_uninit(s);
    delete s;
}

static void play_oneshot(ma_engine* engine, AudioVoice* pool, int pool_size,
    int& head, const std::string& path, const glm::vec3& pos, float volume = 1.0f)
{
    if (path.empty() || !std::filesystem::exists(path)) return;

    // evict oldest voice at head
    AudioVoice& slot = pool[head];
    if (slot.sound){
        ma_sound_stop(slot.sound);
        free_sound(slot.sound);
        slot.sound = nullptr;
    }

    slot.sound = alloc_sound(engine, path, false, true, volume);
    if (!slot.sound) return;

    ma_sound_set_position(slot.sound, pos.x, pos.y, pos.z);
    ma_sound_start(slot.sound);
    slot.in_use = true;

    head = (head + 1) % pool_size;
}

static void scan_radio(AudioSystem& audio, const std::string& assets_dir){
    audio.radio_playlist.clear();
    std::string radio_dir = assets_dir + "/audio/radio";
    if (!std::filesystem::exists(radio_dir)) return;
    for (const auto& e : std::filesystem::directory_iterator(radio_dir)){
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().string();
        if (ext == ".wav" || ext == ".ogg")
            audio.radio_playlist.push_back(e.path().string());
    }
    std::sort(audio.radio_playlist.begin(), audio.radio_playlist.end());
    std::cout << "[audio] radio playlist: " << audio.radio_playlist.size() << " tracks\n";
}

bool audio_init(AudioSystem& audio, const char* assets_dir){
    audio.engine = new ma_engine{};
    if (ma_engine_init(nullptr, audio.engine) != MA_SUCCESS){
        std::cerr << "[audio] failed to init engine\n";
        delete audio.engine;
        audio.engine = nullptr;
        return false;
    }

    std::string adir = std::string(assets_dir);

    // engine layers - non-spatial loops
    auto try_load_loop = [&](const std::string& rel) -> ma_sound* {
        std::string full = adir + "/audio/engine/" + rel;
        return alloc_sound(audio.engine, full, true, false);
    };
    audio.eng_idle = try_load_loop("engine_idle.wav");
    audio.eng_mid = try_load_loop("engine_mid.wav");
    audio.eng_high = try_load_loop("engine_high.wav");

    // start engine idle immediately, mid/high at zero volume
    if (audio.eng_idle){ ma_sound_set_volume(audio.eng_idle, 1.0f); ma_sound_start(audio.eng_idle); }
    if (audio.eng_mid) { ma_sound_set_volume(audio.eng_mid,  0.0f); ma_sound_start(audio.eng_mid);  }
    if (audio.eng_high){ ma_sound_set_volume(audio.eng_high, 0.0f); ma_sound_start(audio.eng_high); }

    // ambient loop
    std::string amb = adir + "/audio/ambience/ambient_loop.wav";
    audio.ambient = alloc_sound(audio.engine, amb, true, false, 1.0f);
    if (audio.ambient) ma_sound_start(audio.ambient);

    // rain loop
    std::string rain_path = adir + "/audio/ambience/rain_loop.wav";
    audio.rain = alloc_sound(audio.engine, rain_path, true, false, 0.0f);
    if (audio.rain) ma_sound_start(audio.rain);

    std::memset(audio.ambience_slots, 0, sizeof(audio.ambience_slots));

    ma_engine_set_volume(audio.engine, 3.0f);

    scan_radio(audio, adir);

    std::memset(audio.impact_pool, 0, sizeof(audio.impact_pool));
    std::memset(audio.voice_pool, 0, sizeof(audio.voice_pool));
    std::memset(audio.step_pool, 0, sizeof(audio.step_pool));
    std::memset(audio.ambience_slots, 0, sizeof(audio.ambience_slots));

    audio.initialized = true;
    std::cout << "[audio] initialized\n";
    return true;
}

void audio_shutdown(AudioSystem& audio){
    if (!audio.initialized) return;

    free_sound(audio.eng_idle);
    free_sound(audio.eng_mid);
    free_sound(audio.eng_high);
    free_sound(audio.ambient);
    free_sound(audio.radio);
    free_sound(audio.rain);

    for (int i = 0; i < Const::MAX_AMBIENCE_SLOTS; i++){
        if (audio.ambience_slots[i].sound){
            free_sound(audio.ambience_slots[i].sound);
            audio.ambience_slots[i].sound = nullptr;
            audio.ambience_slots[i].playing = false;
        }
    }

    for (int i = 0; i < Const::AUDIO_IMPACT_VOICES; i++) free_sound(audio.impact_pool[i].sound);
    for (int i = 0; i < Const::AUDIO_VOICE_VOICES; i++) free_sound(audio.voice_pool[i].sound);
    for (int i = 0; i < Const::AUDIO_STEP_VOICES; i++) free_sound(audio.step_pool[i].sound);

    ma_engine_uninit(audio.engine);
    delete audio.engine;
    audio.engine = nullptr;
    audio.initialized = false;
    std::cout << "[audio] shutdown\n";
}

void audio_update(AudioSystem& audio, float dt,
    const glm::vec3& listener_pos, const glm::vec3& listener_fwd,
    float trike_speed, float trike_max_speed, bool is_driving)
{
    if (!audio.initialized) return;

    // update 3D listener
    ma_engine_listener_set_position(audio.engine, 0,
        listener_pos.x, listener_pos.y, listener_pos.z);
    ma_engine_listener_set_direction(audio.engine, 0,
        listener_fwd.x, listener_fwd.y, listener_fwd.z);
    ma_engine_listener_set_world_up(audio.engine, 0, 0.0f, 1.0f, 0.0f);

    // engine layer crossfade
    if (is_driving){
        float t = glm::clamp(std::abs(trike_speed) / trike_max_speed, 0.0f, 1.0f);

        // idle fades out as speed rises
        float idle_vol = glm::mix(1.0f, 0.0f, glm::smoothstep(0.0f, 0.5f, t));
        // mid peaks at mid speed
        float mid_vol = glm::smoothstep(0.0f, 0.4f, t) * (1.0f - glm::smoothstep(0.6f, 1.0f, t));
        // high fades in at high speed
        float high_vol = glm::smoothstep(0.5f, 1.0f, t);

        // pitch shift: 0.8 at idle, 1.4 at full throttle
        float pitch = 0.8f + t * 0.6f;

        if (audio.eng_idle) { ma_sound_set_volume(audio.eng_idle, idle_vol); ma_sound_set_pitch(audio.eng_idle, pitch); }
        if (audio.eng_mid)  { ma_sound_set_volume(audio.eng_mid,  mid_vol);  ma_sound_set_pitch(audio.eng_mid,  pitch); }
        if (audio.eng_high) { ma_sound_set_volume(audio.eng_high, high_vol); ma_sound_set_pitch(audio.eng_high, pitch); }
    }
    else {
        // on foot - kill engine audio
        if (audio.eng_idle) ma_sound_set_volume(audio.eng_idle, 0.0f);
        if (audio.eng_mid)  ma_sound_set_volume(audio.eng_mid,  0.0f);
        if (audio.eng_high) ma_sound_set_volume(audio.eng_high, 0.0f);
    }

    // radio track end detection - auto advance
    if (audio.radio_on && audio.radio && !ma_sound_is_playing(audio.radio)){
        audio_radio_next(audio);
    }

    // tick impact cooldown
    if (audio.impact_cooldown > 0.0f)
        audio.impact_cooldown -= dt;

    // clean up finished one-shots
    for (int i = 0; i < Const::AUDIO_IMPACT_VOICES; i++){
        auto& v = audio.impact_pool[i];
        if (v.sound && v.in_use && !ma_sound_is_playing(v.sound)){
            free_sound(v.sound);
            v.sound  = nullptr;
            v.in_use = false;
        }
    }
    for (int i = 0; i < Const::AUDIO_VOICE_VOICES; i++){
        auto& v = audio.voice_pool[i];
        if (v.sound && v.in_use && !ma_sound_is_playing(v.sound)){
            free_sound(v.sound);
            v.sound  = nullptr;
            v.in_use = false;
        }
    }
}

void audio_trigger_impact(AudioSystem& audio,
    const std::string& path, const glm::vec3& pos, float force)
{
    if (!audio.initialized || path.empty()) return;
    if (audio.impact_cooldown > 0.0f) return;
    float vol = glm::clamp(0.4f + force * 0.08f, 0.3f, 1.0f);
    play_oneshot(audio.engine, audio.impact_pool, Const::AUDIO_IMPACT_VOICES,
        audio.impact_head, path, pos, vol);
    audio.impact_cooldown = AudioSystem::IMPACT_COOLDOWN_INTERVAL;
}

void audio_trigger_voice(AudioSystem& audio,
    const std::string& path, const glm::vec3& pos)
{
    if (!audio.initialized || path.empty()) return;
    play_oneshot(audio.engine, audio.voice_pool, Const::AUDIO_VOICE_VOICES,
        audio.voice_head, path, pos, 1.0f);
}

void audio_trigger_voice_local(AudioSystem& audio, const std::string& path){
    if (!audio.initialized || path.empty()) return;
    // non-spatial always full volume we use this for mount npc voices
    glm::vec3 dummy = glm::vec3(0.0f);
    ma_sound* s = alloc_sound(audio.engine, path, false, false, 1.0f);
    if (!s) return;
    AudioVoice& slot = audio.voice_pool[audio.voice_head];
    if (slot.sound){
        ma_sound_stop(slot.sound);
        ma_sound_uninit(slot.sound);
        delete slot.sound;
        slot.sound = nullptr;
    }
    slot.sound = s;
    ma_sound_start(slot.sound);
    slot.in_use = true;
    audio.voice_head = (audio.voice_head + 1) % Const::AUDIO_VOICE_VOICES;
}

void audio_trigger_step(AudioSystem& audio,
    const std::string& path, float anim_timer_delta){
    if (!audio.initialized || path.empty()) return;
    audio.step_timer += anim_timer_delta;
    if (audio.step_timer < audio.step_interval) return;
    audio.step_timer = 0.0f;

    // build variant list from the file's directory, cached per directory
    // e.g. assets/audio/footstep/concrete/concrete1.wav
    //   -> scans assets/audio/footstep/type/variant1 for all wavs once picks randomly each step
    static std::unordered_map<std::string, std::vector<std::string>> s_variants;
    std::string dir = path;
    if (s_variants.find(dir) == s_variants.end()){
        auto& list = s_variants[dir];
        if (std::filesystem::exists(dir)){
            for (const auto& e : std::filesystem::directory_iterator(dir)){
                if (!e.is_regular_file()) continue;
                auto ext = e.path().extension().string();
                if (ext == ".wav" || ext == ".ogg")
                    list.push_back(e.path().string());
            }
            std::sort(list.begin(), list.end());
        }
        // fallback: if scan empty or dir missing, use the path itself
        if (list.empty()) list.push_back(path);
        std::cout << "[audio] step variants for " << dir << ": " << list.size() << "\n";
    }

    const auto& variants = s_variants[dir];
    const std::string& pick = variants[rand() % variants.size()];
    AudioVoice& slot = audio.step_pool[audio.step_head];
    if (slot.sound){
        ma_sound_stop(slot.sound);
        free_sound(slot.sound);
        slot.sound = nullptr;
    }
    slot.sound = alloc_sound(audio.engine, pick, false, false, 0.7f);
    if (slot.sound) ma_sound_start(slot.sound);
    slot.in_use = true;
    audio.step_head = (audio.step_head + 1) % Const::AUDIO_STEP_VOICES;
}

void audio_radio_next(AudioSystem& audio){
    if (audio.radio_playlist.empty()) return;
    audio.radio_index = (audio.radio_index + 1) % (int)audio.radio_playlist.size();
    free_sound(audio.radio);
    audio.radio = alloc_sound(audio.engine,
        audio.radio_playlist[audio.radio_index], false, false, audio.radio_volume);
    if (audio.radio) ma_sound_start(audio.radio);
    std::cout << "[audio] radio -> " << audio.radio_playlist[audio.radio_index] << "\n";
}

void audio_radio_toggle(AudioSystem& audio){
    if (!audio.initialized) return;
    audio.radio_on = !audio.radio_on;
    if (audio.radio_on){
        if (!audio.radio && !audio.radio_playlist.empty()){
            audio.radio = alloc_sound(audio.engine,
                audio.radio_playlist[audio.radio_index], false, false, audio.radio_volume);
        }
        if (audio.radio) ma_sound_start(audio.radio);
    }
    else {
        if (audio.radio) ma_sound_stop(audio.radio);
    }
    std::cout << "[audio] radio " << (audio.radio_on ? "ON" : "OFF") << "\n";
}

void audio_radio_set_volume(AudioSystem& audio, float vol){
    audio.radio_volume = glm::clamp(vol, 0.0f, 1.0f);
    if (audio.radio) ma_sound_set_volume(audio.radio, audio.radio_volume);
}

void audio_rain_set(AudioSystem& audio, bool active){
    audio.rain_active = active;
    // actual volume lerp happens in audio_update_env each frame
}

void audio_update_env(AudioSystem& audio, float dt,
    const glm::vec3& listener_pos,
    const AmbienceZone* zones, int zone_count,
    float night_factor)
{
    if (!audio.initialized) return;

    // rain crossfade 
    float rain_target = audio.rain_active ? Const::RAIN_VOL_MAX : 0.0f;
    audio.rain_vol = audio.rain_vol + glm::clamp(rain_target - audio.rain_vol,
        -Const::FADE_SPEED * dt, Const::FADE_SPEED * dt);
    if (audio.rain) ma_sound_set_volume(audio.rain, audio.rain_vol);

    // per zone slot update
    // mark all slots as unclaimed this frame
    bool claimed[Const::MAX_AMBIENCE_SLOTS] = {};

    for (int z = 0; z < zone_count; z++){
        const AmbienceZone& zone = zones[z];

        if (zone.type == AMBIENCE_NIGHT && night_factor < Const::NIGHT_THRESH) continue;

        float dist = glm::length(listener_pos - zone.pos);
        if (dist >= zone.radius) continue; // outside, don't need slot

        int slot_idx = -1;
        for (int i = 0; i < Const::MAX_AMBIENCE_SLOTS; i++){
            if (audio.ambience_slots[i].zone_id == zone.id){
                slot_idx = i;
                break;
            }
        }
        if (slot_idx == -1){
            // grab first free slot
            for (int i = 0; i < Const::MAX_AMBIENCE_SLOTS; i++){
                if (audio.ambience_slots[i].zone_id == -1){
                    slot_idx = i;
                    break;
                }
            }
        }
        if (slot_idx == -1) continue; // all slots busy, so skip

        auto& slot = audio.ambience_slots[slot_idx];
        claimed[slot_idx] = true;

        // load sound if slot just acquired
        if (slot.zone_id != zone.id){
            if (slot.sound) free_sound(slot.sound);
            std::string full_path = std::string("../assets/") + zone.audio_path;
            slot.sound = alloc_sound(audio.engine, full_path, true, false, 0.0f);
            slot.zone_id = zone.id;
            slot.cur_vol = 0.0f;
            slot.playing = false;
            if (slot.sound){ ma_sound_start(slot.sound); slot.playing = true; }
        }

        // target volume: linear falloff from zone center, max 1.0 at center
        float t = 1.0f - (dist / zone.radius);
        float target_vol = glm::smoothstep(0.0f, 1.0f, t);

        // lerp toward target
        slot.cur_vol = slot.cur_vol + glm::clamp(target_vol - slot.cur_vol,
            -Const::FADE_SPEED * dt, Const::FADE_SPEED * dt);
        if (slot.sound) ma_sound_set_volume(slot.sound, slot.cur_vol);
    }

    // release unclaimed slots (listener left zone or zone became inactive)
    for (int i = 0; i < Const::MAX_AMBIENCE_SLOTS; i++){
        if (audio.ambience_slots[i].zone_id == -1) continue;
        if (claimed[i]) continue;

        auto& slot = audio.ambience_slots[i];

        // fade out
        slot.cur_vol = slot.cur_vol - Const::FADE_SPEED * dt;
        if (slot.cur_vol <= 0.0f){
            if (slot.sound){ free_sound(slot.sound); slot.sound = nullptr; }
            slot.zone_id = -1;
            slot.cur_vol = 0.0f;
            slot.playing = false;
        } 
        else {
            if (slot.sound) ma_sound_set_volume(slot.sound, slot.cur_vol);
        }
    }
}