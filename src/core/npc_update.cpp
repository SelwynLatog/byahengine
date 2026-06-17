#include "../renderer/editor_renderer.hpp"
#include "../world/height_field.hpp"
#include "../audio/audio.hpp"
#include "npc_update.hpp"
#include "const.hpp"
#include <filesystem>
#include <iostream>

void init_npcs(App& app){
    app.npcs.clear();
    app.passenger_npc_id = -1;
    app.passenger_fare   = 0.0f;

    for (const auto& o : app.map.objects){
        if (o.behavior != PEDESTRIAN) continue;

        if (app.npc_model_cache.find(o.model_path) == app.npc_model_cache.end()){
            std::string full_path = "../assets/" + o.model_path;
            if (std::filesystem::exists(full_path)){
                driver_model_init(app.npc_model_cache[o.model_path], full_path.c_str(), (NpcType)o.npc_type);
                std::cout << "[npc] loaded model " << o.model_path << "\n";
            }
            else {
                app.npc_model_cache[o.model_path]; // default-construct, remapped below
                std::cout << "[npc] model not found: " << o.model_path << ", using DRIVER fallback\n";
            }
        }

        NpcState npc;
        npc_init(npc, o.id,
            (NpcType)o.npc_type,
            o.position, o.rotation.y,
            o.npc_walk_a, o.npc_walk_b,
            o.npc_can_hail, o.npc_drop_point,
            o.npc_weight);

        npc.model_path = o.model_path;
        npc.editor_scale = o.scale;
        npc.editor_yaw = o.rotation.y;
        npc.editor_y_floor_offset = editor_get_y_floor_offset(app.editor_renderer, o.model_path);

        for (int i = 0; i < BONE_COUNT; i++){
            npc.hail_pose_quat[i] = o.npc_hail_quat[i];
            npc.hail_pose_offset[i] = o.npc_hail_offset[i];
            npc.mount_pose_quat[i] = o.npc_mount_quat[i];
            npc.mount_pose_offset[i] = o.npc_mount_offset[i];
        }
        npc.hail_pose_seat  = o.npc_hail_seat;
        npc.mount_pose_seat = o.npc_mount_seat;
        app.npcs.push_back(npc);
    }
    std::cout << "[npc] spawned " << app.npcs.size() << " npcs\n";
}

void npc_update(App& app, float dt, bool s_q_pickup){
    static constexpr float NPC_HAIL_RANGE_SQ = 36.0f; // 6m

    for (auto& npc : app.npcs){
        glm::vec3 dnpc = npc.position - app.trike.position;
        dnpc.y = 0.0f;
        if (glm::dot(dnpc, dnpc) > Const::NPC_CULL_DIST_SQ) continue;

        // passenger: lock to sidecar position
        if (npc.mode == NPC_PASSENGER){
            auto npc_mdl_it = app.npc_model_cache.find(npc.model_path);
            const DriverModel& npc_mdl = (npc_mdl_it != app.npc_model_cache.end())
                ? npc_mdl_it->second : app.scene.driver_model;

            float c = std::cos(app.trike.heading);
            float s = std::sin(app.trike.heading);

            // sidecar offset: x=fwd/back  y=up/down  z=right/left
            glm::vec3 sidecar_off = glm::vec3(0.2f, 0.0f, 0.1f);
            npc.position = app.trike.position + glm::vec3(
                c * sidecar_off.x - s * sidecar_off.z,
                sidecar_off.y,
                s * sidecar_off.x + c * sidecar_off.z);
            npc.yaw = -app.trike.heading + glm::radians(90.0f) - npc_mdl.forward_offset + glm::pi<float>();

            app.passenger_fare += std::abs(app.trike.speed) * dt * Const::FARE_RATE_PER_METRE;
            app.passenger_time += dt;

            // NPC voice triggers: rollover > heavy crash > mild
            // per-npc cooldown reuses hail_wave_timer (idle during passenger)
            bool fresh_crash = (app.trike.impact_timer > 0.28f);
            bool genuinely_airborne = (app.trike.is_airborne && app.trike.air_time > 0.4f);
            if ((fresh_crash || genuinely_airborne) && npc.hail_wave_timer <= 0.0f){
                for (const auto& o : app.map.objects){
                    if (o.id != npc.id) continue;
                    if ((app.trike.is_rolled_over || genuinely_airborne) && !o.audio_crash_rollover.empty()){
                        audio_trigger_voice_local(app.audio, "../assets/" + o.audio_crash_rollover);
                        npc.hail_wave_timer = 4.0f;
                    }
                    else if (app.trike.last_impact_force > 3.5f && !o.audio_crash_heavy.empty()){
                        audio_trigger_voice_local(app.audio, "../assets/" + o.audio_crash_heavy);
                        npc.hail_wave_timer = 2.5f;
                    }
                    else if (app.trike.last_impact_force > 1.0f && !o.audio_crash_mild.empty()){
                        audio_trigger_voice_local(app.audio, "../assets/" + o.audio_crash_mild);
                        npc.hail_wave_timer = 1.5f;
                    }
                    break;
                }
            }
            if (npc.hail_wave_timer > 0.0f) npc.hail_wave_timer -= dt;
            if (npc.hail_wave_timer > 2.0f) app.trike.last_impact_force = 0.0f;

            // check arrival at drop point
            glm::vec3 to_drop = npc.drop_point - app.trike.position;
            to_drop.y = 0.0f;
            if (glm::dot(to_drop, to_drop) < 4.0f){
                float side_angle = app.trike.heading + glm::half_pi<float>();
                npc.position = app.trike.position
                    + glm::vec3(std::cos(side_angle), 0.0f, std::sin(side_angle)) * 1.8f;
                npc.position.y = heightfield_sample(app.map.terrain,
                    npc.position.x, npc.position.z);
                npc.mode = NPC_DISMOUNTING;
                npc.walk_forward = false;
                npc.hail_timer = 15.0f;
                app.passenger_npc_id = -1;
                //std::cout << "[npc] dropoff fare=" << app.passenger_fare << "\n";

                for (const auto& o : app.map.objects){
                    if (o.id != npc.id) continue;
                    bool slow = (app.passenger_time > Const::DROPOFF_SLOW_THRESHOLD);
                    const std::string& vpath = slow ? o.audio_dropoff_bad : o.audio_dropoff_good;
                    if (!vpath.empty())
                        audio_trigger_voice(app.audio, "../assets/" + vpath, npc.position);
                    break;
                }
                app.passenger_fare = 0.0f;
                app.passenger_time = 0.0f;
            }
            continue;
        }

        npc_update(npc, app.map.terrain, dt, app.trike.position);

        // hailing: only idle/walk npcs in range
        // no current passenger
        if (npc.can_hail
            && npc.mode != NPC_RAGDOLL
            && npc.mode != NPC_HAILING
            && npc.mode != NPC_MOUNTING
            && npc.mode != NPC_DISMOUNTING
            && app.passenger_npc_id == -1)
        {
            glm::vec3 d = npc.position - app.trike.position;
            d.y = 0.0f;
            if (glm::dot(d, d) < NPC_HAIL_RANGE_SQ && npc.hail_timer <= 0.0f){
                npc.mode = NPC_HAILING;
                std::cout << "[npc] id=" << npc.id << " hailing\n";
                for (const auto& o : app.map.objects){
                    if (o.id != npc.id) continue;
                    if (!o.audio_hail.empty())
                        audio_trigger_voice(app.audio, "../assets/" + o.audio_hail, npc.position);
                    break;
                }
            }
        }

        // hailing: face trike
        // confirm pickup on Q
        if (npc.mode == NPC_HAILING){
            glm::vec3 d = app.trike.position - npc.position;
            d.y = 0.0f;
            if (glm::length(d) > 0.1f) npc.yaw = std::atan2(d.z, d.x);

            glm::vec3 to_npc = npc.position - app.trike.position;
            to_npc.y = 0.0f;
            if (glm::dot(to_npc, to_npc) < 9.0f
                && std::abs(app.trike.speed) < 1.5f
                && app.passenger_npc_id == -1
                && s_q_pickup)
            {
                npc.mode = NPC_PASSENGER;
                app.passenger_npc_id = npc.id;
                app.passenger_time   = 0.0f;
                std::cout << "[npc] id=" << npc.id << " picked up\n";
                for (const auto& o : app.map.objects){
                    if (o.id != npc.id) continue;
                    if (!o.audio_pickup.empty())
                        audio_trigger_voice(app.audio, "../assets/" + o.audio_pickup, npc.position);
                    break;
                }
            }
        }

        // idle ambient chatter
        if (npc.can_hail && (npc.mode == NPC_IDLE || npc.mode == NPC_WALK)
            && npc.yap_timer <= 0.0f)
        {
            for (const auto& o : app.map.objects){
                if (o.id != npc.id) continue;
                if (!o.audio_yap.empty()){
                    audio_trigger_voice(app.audio, "../assets/" + o.audio_yap, npc.position);
                    npc.yap_timer = 8.0f + (float)(npc.id % 13) * 0.9f;
                }
                break;
            }
        }
        if (npc.yap_timer > 0.0f) npc.yap_timer -= dt;

        // trike vs NPC ragdoll collision
        if (npc.mode != NPC_RAGDOLL && npc.mode != NPC_PASSENGER){
            glm::vec3 d = npc.position - app.trike.position;
            d.y = 0.0f;
            if (glm::dot(d, d) < 1.2f * 1.2f){
                glm::vec3 trike_fwd = { std::cos(app.trike.heading), 0.0f, std::sin(app.trike.heading) };
                float closing = app.trike.speed;
                if (std::abs(closing) > 0.5f){
                    glm::vec3 impulse = trike_fwd * closing * 0.8f;
                    npc_hit(npc, impulse);
                    if (npc.id == app.passenger_npc_id) app.passenger_npc_id = -1;
                    if (closing > 1.0f){
                        app.trike.last_impact_force = closing;
                        app.trike.impact_timer = glm::clamp(closing * 0.08f, 0.15f, 0.35f);
                        for (const auto& o : app.map.objects){
                            if (o.id != npc.id) continue;
                            if (!o.audio_impact.empty())
                                audio_trigger_impact(app.audio,
                                    "../assets/" + o.audio_impact,
                                    npc.position, closing);
                            break;
                        }
                    }
                }
            }
        }
    }
}