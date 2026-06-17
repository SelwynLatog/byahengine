#include "../world/height_field.hpp"
#include "../audio/audio.hpp"
#include "../core/const.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "collision.hpp"
#include "trike_aabb.hpp"
#include <cmath>
#include <iostream>

void world_map_to_obstacles(App& app){
    // tag generated objects by rebuilding from scratch each time
    // im sure there's definitely a much more efficient way but for now
    // this is fine :>
    app.obstacles_dirty = false;
    app.obstacles.clear();

    for (const auto& o : app.map.objects){
        if (o.behavior != STATIC) continue;

        glm::vec3 world_min, world_max;
        auto bit = app.editor_renderer.prop_bounds.find(o.model_path);

        if (bit != app.editor_renderer.prop_bounds.end()){
            float yoff = app.editor_renderer.prop_y_offset.count(o.model_path)
                ? app.editor_renderer.prop_y_offset.at(o.model_path) : 0.0f;

            glm::vec3 lmin = bit->second.local_min;
            glm::vec3 lmax = bit->second.local_max;
            glm::vec3 smin = { lmin.x * o.scale.x, (lmin.y + yoff) * o.scale.y, lmin.z * o.scale.z };
            glm::vec3 smax = { lmax.x * o.scale.x, (lmax.y + yoff) * o.scale.y, lmax.z * o.scale.z };
            float c = std::cos(o.rotation.y), s = std::sin(o.rotation.y);
            world_min = glm::vec3( 1e9f);
            world_max = glm::vec3(-1e9f);

            // rotate all 8 AABB corners by object yaw to get tight world-space bounds
            for (int k = 0; k < 8; k++){
                glm::vec3 corner = {
                    (k & 1) ? smax.x : smin.x,
                    (k & 2) ? smax.y : smin.y,
                    (k & 4) ? smax.z : smin.z,
                };
                glm::vec3 world = o.position + glm::vec3(
                    c * corner.x - s * corner.z, corner.y, s * corner.x + c * corner.z);
                world_min = glm::min(world_min, world);
                world_max = glm::max(world_max, world);
            }
        }
        else {
            // no mesh bounds cached, fallback to scale-based box
            glm::vec3 half = o.scale * 0.5f;
            world_min = o.position + glm::vec3(-half.x, 0.0f, -half.z);
            world_max = o.position + glm::vec3( half.x, o.scale.y, half.z);
        }

        glm::vec3 half   = (world_max - world_min) * 0.5f;
        glm::vec3 center = (world_min + world_max) * 0.5f;

        Obstacle obs;
        obs.position     = glm::vec3(center.x, world_min.y, center.z); // center-bottom for ref
        obs.half_extents = half;
        obs.aabb.min     = world_min;
        obs.aabb.max     = world_max;
        obs.world_id     = o.id;
        app.obstacles.push_back(obs);
    }
    std::cout << "[collision] rebuilt " << app.obstacles.size() << " obstacles from world map\n";
}

bool collision_static_update(App& app, float dt, float& pre_collision_speed, bool& any_collision){
    if (app.trike.is_tipping || app.trike.is_rolled_over) return false;

    for (auto& obs : app.obstacles){
        if (obs.hit_timer > 0.0f) obs.hit_timer -= dt;
        glm::vec3 to_obs = obs.position - app.trike.position;
        if (glm::dot(to_obs, to_obs) > 25.0f) continue;
        if (!aabb_overlap(app.trike.aabb, obs.aabb)) continue;

        // skip full response if we just hit this obstacle recently
        // MTV still separates, but no force injection until cooldown expires
        bool fresh_hit = (obs.hit_timer <= 0.0f);

        float speed_before = std::abs(app.trike.speed);
        float lat_before   = std::abs(app.trike.lateral_speed);

        // min translation vector: smallest push to separate the two boxes
        glm::vec3 mtv = aabb_mtv(app.trike.aabb, obs.aabb);
        app.trike.position += mtv;

        glm::vec3 mtv_normal = glm::length(mtv) > 0.0f ? glm::normalize(mtv) : glm::vec3(0.0f);

        glm::vec3 fwd = { std::cos(app.trike.heading), 0.0f, std::sin(app.trike.heading) };
        glm::vec3 rgt = { std::cos(app.trike.heading + glm::half_pi<float>()), 0.0f,
                          std::sin(app.trike.heading + glm::half_pi<float>()) };

        float spd_dot  = glm::dot(fwd, mtv_normal);
        float lat_dot  = glm::dot(rgt, mtv_normal);
        float spd_along = app.trike.speed        * spd_dot;
        float lat_along = app.trike.lateral_speed * lat_dot;

        float closing = 0.0f;
        if (spd_along < 0.0f) closing += std::abs(spd_along);
        if (lat_along < 0.0f) closing += std::abs(lat_along);

        if (closing > 0.8f && fresh_hit){
            app.trike.last_impact_force = closing;
            app.trike.impact_timer = (closing > 2.0f) ? 0.35f : 0.0f;
            obs.hit_timer = 0.35f;

            auto wit = app.wo_by_id.find(obs.world_id);
            if (wit != app.wo_by_id.end() && !wit->second->audio_impact.empty())
                audio_trigger_impact(app.audio,
                    "../assets/" + wit->second->audio_impact,
                    obs.position, closing);

            if (spd_along < 0.0f)
                app.trike.speed += (-spd_along) * (1.0f + Const::RESTITUTION) * spd_dot;
            if (lat_along < 0.0f)
                app.trike.lateral_speed += (-lat_along) * (1.0f + Const::RESTITUTION) * lat_dot;

            float bleed = glm::clamp(closing * 0.06f, 0.05f, 0.4f);
            app.trike.speed         *= (1.0f - bleed);
            app.trike.lateral_speed *= (1.0f - bleed);

            float side_factor = std::abs(lat_dot);
            if (side_factor > 0.3f && closing > 1.5f)
                app.trike.roll_rate += side_factor * closing * 0.4f
                    * (lat_dot > 0.0f ? 1.0f : -1.0f);
        }
        else {
            if (spd_along < 0.0f) app.trike.speed        -= spd_along;
            if (lat_along < 0.0f) app.trike.lateral_speed -= lat_along;
        }

        // project full velocity and cancel the wall-normal component entirely
        // prevents trike re-entering the wall next tick
        glm::vec3 fwd2 = { std::cos(app.trike.heading), 0.0f, std::sin(app.trike.heading) };
        glm::vec3 rgt2 = { std::cos(app.trike.heading + glm::half_pi<float>()), 0.0f,
                           std::sin(app.trike.heading + glm::half_pi<float>()) };
        glm::vec3 vel = fwd2 * app.trike.speed + rgt2 * app.trike.lateral_speed;
        float vel_into = glm::dot(vel, -mtv_normal);
        if (vel_into > 0.0f){
            glm::vec3 vel_corrected = vel + mtv_normal * vel_into;
            app.trike.speed        = glm::dot(vel_corrected, fwd2);
            app.trike.lateral_speed = glm::dot(vel_corrected, rgt2);
        }

        // hard clamp: zero any residual wall-penetrating component
        float residual = glm::dot(
            fwd2 * app.trike.speed + rgt2 * app.trike.lateral_speed, -mtv_normal);
        if (residual > 0.0f){
            app.trike.speed        -= residual * glm::dot(fwd2, -mtv_normal);
            app.trike.lateral_speed -= residual * glm::dot(rgt2, -mtv_normal);
        }
        app.trike.lateral_speed *= 0.55f;

        // cap exit velocity to avoid restitution overshoot
        float max_exit = speed_before * (1.0f + Const::RESTITUTION);
        if (std::abs(app.trike.speed) > max_exit)
            app.trike.speed = std::copysign(max_exit, app.trike.speed);
        if (std::abs(app.trike.lateral_speed) > lat_before * (1.0f + Const::RESTITUTION))
            app.trike.lateral_speed = std::copysign(lat_before, app.trike.lateral_speed);

        // kill residual ghost velocity after every wall contact
        app.trike.lateral_speed = 0.0f;
        if (std::abs(app.trike.speed) < 0.5f) app.trike.speed = 0.0f;

        any_collision = true;
        aabb_update(app.trike.aabb, app.trike.position, app.trike.heading);
    }
    return any_collision;
}

void collision_dynamic_update(App& app, float dt){
    for (auto& [id, sim] : app.dynamic_sims){
        auto wit = app.wo_by_id.find(id);
        if (wit == app.wo_by_id.end()) continue;
        const WorldObject* wo = wit->second;

        // rebuild AABB from current sim position — only awake objects need this every frame
        if (!sim.sleeping){
            auto bit = app.editor_renderer.prop_bounds.find(wo->model_path);
            if (bit != app.editor_renderer.prop_bounds.end()){
                float yoff = app.editor_renderer.prop_y_offset.count(wo->model_path)
                    ? app.editor_renderer.prop_y_offset.at(wo->model_path) : 0.0f;
                glm::vec3 lmin = bit->second.local_min;
                glm::vec3 lmax = bit->second.local_max;
                sim.aabb.min = sim.position + glm::vec3(lmin.x*wo->scale.x, (lmin.y+yoff)*wo->scale.y, lmin.z*wo->scale.z);
                sim.aabb.max = sim.position + glm::vec3(lmax.x*wo->scale.x, (lmax.y+yoff)*wo->scale.y, lmax.z*wo->scale.z);
            }
        }

        // trike vs dynamic collision
        if (aabb_overlap(app.trike.aabb, sim.aabb)){
            sim.sleeping = false;
            glm::vec3 mtv = aabb_mtv(app.trike.aabb, sim.aabb);
            glm::vec3 hit_normal = glm::length(mtv) > 0.0f ? glm::normalize(mtv) : glm::vec3(0,0,1);

            float total_mass = wo->mass + Const::TRIKE_MASS;
            float obj_share   = Const::TRIKE_MASS / total_mass;
            float trike_share = wo->mass / total_mass;

            app.trike.position -= mtv * trike_share;
            sim.position       += mtv * obj_share;

            glm::vec3 trike_fwd = { std::cos(app.trike.heading), 0.0f, std::sin(app.trike.heading) };
            float closing = std::abs(app.trike.speed) * std::abs(glm::dot(trike_fwd, -hit_normal));

            float impulse = closing * (1.0f + wo->restitution) * (Const::TRIKE_MASS / total_mass);
            sim.velocity += -hit_normal * impulse;

            // topple torque away from hit direction
            glm::vec3 push_dir = -hit_normal;
            float height  = sim.aabb.max.y - sim.aabb.min.y;
            float inertia = wo->mass * height * height * 0.3f;
            float torque_mag = impulse * height * 0.5f / (inertia + 0.001f);

            glm::vec3 push_xz = glm::vec3(push_dir.x, 0.0f, push_dir.z);
            if (glm::length(push_xz) > 0.001f) push_xz = glm::normalize(push_xz);

            float obj_yaw = wo->rotation.y;
            glm::vec3 obj_fwd = { std::cos(obj_yaw), 0.0f, std::sin(obj_yaw) };
            glm::vec3 obj_rgt = { std::cos(obj_yaw + glm::half_pi<float>()), 0.0f,
                                  std::sin(obj_yaw + glm::half_pi<float>()) };

            float push_fwd  = glm::dot(push_xz, obj_fwd);
            float push_side = glm::dot(push_xz, obj_rgt);
            sim.pitch_vel += torque_mag * push_fwd;
            sim.roll_vel += torque_mag * push_side;
            sim.yaw_vel += torque_mag * 0.4f * push_side;

            app.trike.speed *= (1.0f - glm::clamp(wo->mass / total_mass * 0.6f, 0.0f, 0.6f));

            if (closing > 0.8f){
                sim.hit_timer = glm::clamp(closing * 0.12f, 0.15f, 0.45f);
                if (closing > 2.0f){
                    app.trike.last_impact_force = closing;
                    app.trike.impact_timer = glm::clamp(closing * 0.08f, 0.15f, 0.35f);
                }
                if (!wo->audio_impact.empty())
                    audio_trigger_impact(app.audio,
                        "../assets/" + wo->audio_impact,
                        sim.position, closing);
            }
            aabb_update(app.trike.aabb, app.trike.position, app.trike.heading);
        }

        if (sim.sleeping) continue;

        sim.position += sim.velocity * dt;

        float sim_ground = heightfield_sample(app.map.terrain, sim.position.x, sim.position.z);
        bool on_ground = (sim.position.y <= sim_ground + 0.01f);
        if (on_ground){
            sim.position.y = sim_ground;
            sim.vert_vel   = 0.0f;
        }
        else {
            sim.vert_vel   -= Const::GRAVITY * dt;
            sim.position.y += sim.vert_vel * dt;
            if (sim.position.y < sim_ground){
                sim.position.y = sim_ground;
                sim.vert_vel   = 0.0f;
            }
        }

        // static obstacle collision for dynamic objects
        {
            auto bit = app.editor_renderer.prop_bounds.find(wo->model_path);
            if (bit != app.editor_renderer.prop_bounds.end()){
                float yoff = app.editor_renderer.prop_y_offset.count(wo->model_path)
                    ? app.editor_renderer.prop_y_offset.at(wo->model_path) : 0.0f;
                glm::vec3 lmin = bit->second.local_min;
                glm::vec3 lmax = bit->second.local_max;
                sim.aabb.min = sim.position + glm::vec3(lmin.x*wo->scale.x, (lmin.y+yoff)*wo->scale.y, lmin.z*wo->scale.z);
                sim.aabb.max = sim.position + glm::vec3(lmax.x*wo->scale.x, (lmax.y+yoff)*wo->scale.y, lmax.z*wo->scale.z);
            }
        }
        for (const auto& obs : app.obstacles){
            if (obs.world_id == id) continue; // skip self
            if (!aabb_overlap(sim.aabb, obs.aabb)) continue;

            glm::vec3 mtv = aabb_mtv(sim.aabb, obs.aabb);
            sim.position += mtv;

            // rebuild AABB after push so next obstacle test is accurate
            auto bit = app.editor_renderer.prop_bounds.find(wo->model_path);
            if (bit != app.editor_renderer.prop_bounds.end()){
                float yoff = app.editor_renderer.prop_y_offset.count(wo->model_path)
                    ? app.editor_renderer.prop_y_offset.at(wo->model_path) : 0.0f;
                glm::vec3 lmin = bit->second.local_min;
                glm::vec3 lmax = bit->second.local_max;
                sim.aabb.min = sim.position + glm::vec3(lmin.x*wo->scale.x, (lmin.y+yoff)*wo->scale.y, lmin.z*wo->scale.z);
                sim.aabb.max = sim.position + glm::vec3(lmax.x*wo->scale.x, (lmax.y+yoff)*wo->scale.y, lmax.z*wo->scale.z);
            }

            glm::vec3 mtv_normal = glm::length(mtv) > 0.0f ? glm::normalize(mtv) : glm::vec3(0,1,0);
            float vel_into = glm::dot(sim.velocity, -mtv_normal);
            if (vel_into > 0.0f){
                sim.velocity += mtv_normal * vel_into;
                float bleed = glm::clamp(vel_into * 0.3f, 0.0f, 0.6f);
                sim.pitch_vel *= (1.0f - bleed);
                sim.roll_vel  *= (1.0f - bleed);
                sim.yaw_vel   *= (1.0f - bleed);
            }
        }

        float drag = 1.0f - wo->friction * dt;
        if (on_ground)
            sim.velocity *= glm::clamp(drag, 0.0f, 1.0f);

        sim.yaw += sim.yaw_vel   * dt;
        sim.pitch += sim.pitch_vel * dt;
        sim.roll += sim.roll_vel  * dt;

        float ang_drag = glm::clamp(1.0f - wo->friction * 1.5f * dt, 0.0f, 1.0f);
        sim.yaw_vel *= ang_drag;
        sim.pitch_vel *= ang_drag;
        sim.roll_vel *= ang_drag;

        // gravity pulls tip down while not yet flat 
        float gravity_torque = Const::GRAVITY * 0.4f;
        if (std::abs(sim.pitch) > 0.05f && std::abs(sim.pitch) < glm::half_pi<float>())
            sim.pitch_vel += std::copysign(gravity_torque, sim.pitch) * dt;
        if (std::abs(sim.roll)  > 0.05f && std::abs(sim.roll)  < glm::half_pi<float>())
            sim.roll_vel += std::copysign(gravity_torque, sim.roll)  * dt;

        // past 90 degrees = object is flat on ground, snap and kill angular
        if (std::abs(sim.pitch) >= glm::half_pi<float>()){
            sim.pitch = std::copysign(glm::half_pi<float>(), sim.pitch);
            sim.pitch_vel = 0.0f;
        }
        if (std::abs(sim.roll) >= glm::half_pi<float>()){
            sim.roll = std::copysign(glm::half_pi<float>(), sim.roll);
            sim.roll_vel = 0.0f;
        }

        if (sim.hit_timer > 0.0f) sim.hit_timer -= dt;

        float lin_spd = glm::length(sim.velocity);
        float ang_spd = std::abs(sim.yaw_vel) + std::abs(sim.pitch_vel) + std::abs(sim.roll_vel);
        bool airborne = (sim.position.y > sim_ground + 0.05f);
        if (!airborne && lin_spd < 0.05f && ang_spd < 0.02f){
            sim.velocity  = glm::vec3(0.0f);
            sim.yaw_vel = sim.pitch_vel = sim.roll_vel = 0.0f;
            sim.sleeping  = true;
        }
    }
}

void collision_dynamic_vs_dynamic(App& app){
    // O(n^2) fine for small barangay object counts
    for (auto& [id_a, sim_a] : app.dynamic_sims){
        if (sim_a.sleeping) continue;
        for (auto& [id_b, sim_b] : app.dynamic_sims){
            if (id_a >= id_b) continue; // skip self and already-checked pairs
            if (!aabb_overlap(sim_a.aabb, sim_b.aabb)) continue;

            auto wa = app.wo_by_id.find(id_a);
            auto wb = app.wo_by_id.find(id_b);
            if (wa == app.wo_by_id.end() || wb == app.wo_by_id.end()) continue;
            const WorldObject* wo_a = wa->second;
            const WorldObject* wo_b = wb->second;
            sim_b.sleeping = false;

            glm::vec3 mtv = aabb_mtv(sim_a.aabb, sim_b.aabb);
            glm::vec3 hit_normal = glm::length(mtv) > 0.0f ? glm::normalize(mtv) : glm::vec3(0,0,1);

            float total_mass = wo_a->mass + wo_b->mass;
            sim_a.position -= mtv * (wo_b->mass / total_mass);
            sim_b.position += mtv * (wo_a->mass / total_mass);

            float restitution = (wo_a->restitution + wo_b->restitution) * 0.5f;
            float rel_vel = glm::dot(sim_a.velocity - sim_b.velocity, hit_normal);
            if (rel_vel > 0.0f){
                float impulse = rel_vel * (1.0f + restitution)
                    / (1.0f/wo_a->mass + 1.0f/wo_b->mass);
                sim_a.velocity -= hit_normal * (impulse / wo_a->mass);
                sim_b.velocity += hit_normal * (impulse / wo_b->mass);

                // only topple if impulse is strong enough relative to object mass
                // a potato nudging a fence should never knock it over
                float topple_threshold_a = wo_a->mass * 0.8f;
                float topple_threshold_b = wo_b->mass * 0.8f;
                if (impulse > topple_threshold_a)
                    sim_a.roll_vel -= (impulse * 0.3f) / (wo_a->mass + 0.001f);
                if (impulse > topple_threshold_b)
                    sim_b.roll_vel += (impulse * 0.3f) / (wo_b->mass + 0.001f);
            }
        }
    }
}