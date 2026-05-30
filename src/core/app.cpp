#include "app.hpp"
#include "const.hpp"
#include "editor_cam.hpp"
#include "editor_input.hpp"
#include "../physics/trike_aabb.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include "../world/world_map.hpp"
#include "../world/road_builder.hpp"
#include <cmath>
#include <iostream>

// this commit ver is much cleaner now. Used to be a horrendous god file
// basically had shaders, draw calls, mesh uploads, everything
// all now moved to scene.cpp
// this file just loops input, physics, collisions, cam, render


// camera state
static float s_cam_yaw = Const::CAM_YAW_DEFAULT;
static float s_cam_pitch = Const::CAM_PITCH_DEFAULT;
static float s_cam_dist = Const::CAM_DIST_DEFAULT;
static glm::vec3 s_cam_pos = glm::vec3(-6.0f, 3.0f, 0.0f);
static bool s_free_cam = false;
static bool s_f_pressed_last = false;

// map editor toggle
// edge trigger so a single tab press flips mode once
static bool s_tab_pressed_last = false;

void world_map_to_obstacles(App& app){

    
    // tag generated objects by rebuilding from scratch each time
    // im sure there's definitely a much more efficient way but for now
    // this is fine :>
    app.obstacles_dirty = false;
    app.obstacles.clear();
    
    for(const auto& o: app.map.objects){
        if (o.behavior != STATIC) continue;

        // then get real mesh bounds from editor renderer cache
        glm::vec3 world_min, world_max;
        auto bit = app.editor_renderer.prop_bounds.find(o.model_path);
        if (bit != app.editor_renderer.prop_bounds.end()){
            float yoff = app.editor_renderer.prop_y_offset.count(o.model_path)
                ? app.editor_renderer.prop_y_offset.at(o.model_path) : 0.0f;
            glm::vec3 lmin = bit->second.local_min;
            glm::vec3 lmax = bit->second.local_max;
            // scale and apply y offset, then rotate all 8 corners by object yaw
            glm::vec3 smin = { lmin.x * o.scale.x, (lmin.y + yoff) * o.scale.y, lmin.z * o.scale.z };
            glm::vec3 smax = { lmax.x * o.scale.x, (lmax.y + yoff) * o.scale.y, lmax.z * o.scale.z };
            float c = std::cos(o.rotation.y), s = std::sin(o.rotation.y);
            world_min = glm::vec3( 1e9f);
            world_max = glm::vec3(-1e9f);

            std::cout << "[obs_local] id=" << o.id
                << " lmin=(" << lmin.x << "," << lmin.y << "," << lmin.z << ")"
                << " lmax=(" << lmax.x << "," << lmax.y << "," << lmax.z << ")"
                << " yoff=" << yoff
                << " smin=(" << smin.x << "," << smin.y << "," << smin.z << ")"
                << " smax=(" << smax.x << "," << smax.y << "," << smax.z << ")\n";

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
        else{
            glm::vec3 half = o.scale * 0.5f;
            world_min = o.position + glm::vec3(-half.x, 0.0f, -half.z);
            world_max = o.position + glm::vec3( half.x, o.scale.y, half.z);
        }

        // world_min/max are already full world bounds from the corner loop
        glm::vec3 half = (world_max - world_min) * 0.5f;
        glm::vec3 center = (world_min + world_max) * 0.5f;

        Obstacle obs;
        obs.position = glm::vec3(center.x, world_min.y, center.z); // center-bottom for ref
        obs.half_extents = half;
        obs.aabb.min = world_min;
        obs.aabb.max = world_max;
        std::cout << "[obs] id=" << o.id << " model=" << o.model_path
            << " scale=(" << o.scale.x << "," << o.scale.y << "," << o.scale.z << ")"
            << " wmin=(" << world_min.x << "," << world_min.y << "," << world_min.z << ")"
            << " wmax=(" << world_max.x << "," << world_max.y << "," << world_max.z << ")"
            << " lmin=(" << 0 << ") pos=(" << o.position.x << "," << o.position.y << "," << o.position.z << ")\n";
        obs.world_id = o.id;
        app.obstacles.push_back(obs);


        std::cout << "[collision] STATIC id=" << o.id << " model=" << o.model_path
            << " half=(" << half.x << "," << half.y << "," << half.z << ")\n";
    }
     std::cout << "[collision] rebuilt " << app.obstacles.size()
        << " obstacles from world map\n";
}

// rigid body dynamic sim init
// called and loaded when returning from editor
void init_dynamic_sims(App& app){
    for (const auto& o : app.map.objects){
        if (o.behavior !=DYNAMIC) continue;

        if (app.dynamic_sims.count(o.id)) continue;

        DynamicSim sim;
        sim.position = o.position;
        sim.yaw = 0.0f;

        // builds initial AABB from prop bounds
        auto bit = app.editor_renderer.prop_bounds.find(o.model_path);
        if (bit != app.editor_renderer.prop_bounds.end()){
            float yoff = app.editor_renderer.prop_y_offset.count(o.model_path)
                ? app.editor_renderer.prop_y_offset.at(o.model_path) : 0.0f;
            glm::vec3 lmin = bit->second.local_min;
            glm::vec3 lmax = bit->second.local_max;
            glm::vec3 smin = { lmin.x*o.scale.x, (lmin.y + yoff)*o.scale.y, lmin.z*o.scale.z };
            glm::vec3 smax = { lmax.x*o.scale.x, (lmax.y + yoff)*o.scale.y, lmax.z*o.scale.z };
            float c = std::cos(o.rotation.y), s = std::sin(o.rotation.y);
            glm::vec3 wmin( 1e9f), wmax(-1e9f);
            for (int k = 0; k < 8; k++){
                glm::vec3 corner = {
                    (k & 1) ? smax.x : smin.x,
                    (k & 2) ? smax.y : smin.y,
                    (k & 4) ? smax.z : smin.z,
                };
                glm::vec3 world = sim.position + glm::vec3(
                    c * corner.x - s * corner.z, corner.y, s * corner.x + c * corner.z);
                wmin = glm::min(wmin, world);
                wmax = glm::max(wmax, world);
            }
            sim.aabb = { wmin, wmax };
        }
        else{
            glm::vec3 half = o.scale * 0.5f;
            sim.aabb = { sim.position - half, sim.position + half };
        }
        app.dynamic_sims[o.id] = sim;
    }
}

void app_init(App& app){
    window_init(app.window,
        Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT, Const::WINDOW_TITLE);

    glEnable(GL_DEPTH_TEST);
    int fb_w, fb_h;
    glfwGetFramebufferSize(app.window.handle, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);

    scene_init(app.scene);
    editor_renderer_init(app.editor_renderer);
    
    road_builder_init("../assets");

    heightfield_init(app.map.terrain,
        Const::TERRAIN_ROWS, Const::TERRAIN_COLS,
        Const::TERRAIN_CELL_SIZE,
        glm::vec3( -(Const::TERRAIN_COLS * Const::TERRAIN_CELL_SIZE) * 0.5f, 0.0f,-(Const::TERRAIN_ROWS * Const::TERRAIN_CELL_SIZE) * 0.5f));
    
    // scan assets/ for placeable props
    editor_scan_props(app.editor, "../assets");

    // try to load existing map
    world_map_load(app.map, Const::MAP_SAVE_PATH);

    for (auto& r : app.map.roads) road_spline_build_mesh(r, &app.map.terrain);
    
    // build collision obstacles from loaded map
    // call prop bounds first to trigger loads via editor_renderer
    for (const auto& o : app.map.objects){
        if (!o.model_path.empty())
            editor_get_y_floor_offset(app.editor_renderer, o.model_path);
    }
    world_map_to_obstacles(app);
    init_dynamic_sims(app);

    // pre editor hardcoded static objs
    // kept for now in case I fuck up my world_map_to_obstacles
    /*
    // spawn static obstacles
    // position=center-bottom 
    // half_extents=half w/h/d

    app.obstacles.push_back(make_obstacle({10.0f, 0.0f,  0.0f}, {0.75f, 1.0f, 0.75f}));
    app.obstacles.push_back(make_obstacle({ 0.0f, 0.0f, 10.0f}, {1.0f,  1.5f, 0.5f}));
    app.obstacles.push_back(make_obstacle({15.0f, 0.0f,  8.0f}, {0.5f,  0.8f, 0.5f}));
    */

    hud_init(app.hud, Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT);

    app.last_time = (float)glfwGetTime();
    app.accumulator = 0.0f;
    app.running = true;
}

// main loop app_run

void app_run(App& app){
    while (!window_should_close(app.window)){
        // delta time
        float now = (float)glfwGetTime();
        float dt = now - app.last_time;
        app.last_time = now;
        if (dt > Const::MAX_DELTA) dt = Const::MAX_DELTA;

        // H key hitbox toggle universal
        static bool s_h_last = false;
        bool h_down = glfwGetKey(app.window.handle, GLFW_KEY_H) == GLFW_PRESS;
        if (h_down && !s_h_last) app.editor.show_hitboxes = !app.editor.show_hitboxes;
        s_h_last = h_down;

        // tab toggle
        // switch from editor/ drive mode
        bool tab_down = glfwGetKey(app.window.handle, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tab_down && !s_tab_pressed_last){
            app.editor.active = !app.editor.active;
            if (app.editor.active){
                // entering editor
                // park cam above trike & reset mouse
                app.editor.cam_pos = app.trike.position + glm::vec3(0.0f, 12.0f, 0.0f);
                editor_cam_init(app.window.handle);
            } 
            else {
                // returning to drive mode
                // only rebuild if map changed since last switch
                if (app.obstacles_dirty){
                    world_map_to_obstacles(app);
                    init_dynamic_sims(app);
                }
            }
        }
        s_tab_pressed_last = tab_down;

        // EDITOR MODE
        if (app.editor.active){
            editor_cam_update(app.editor, app.window.handle, dt);

            glm::mat4 view = editor_cam_get_view(app.editor);
            glm::mat4 proj = glm::perspective ( glm::radians(Const::CAM_FOV), (float)Const::WINDOW_WIDTH/ (float)Const::WINDOW_HEIGHT,
            Const::CAM_NEAR, Const::CAM_FAR);

            editor_input_update(app.editor, app.map, app.editor_renderer, app.window.handle, view, proj, Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT, dt, app.obstacles_dirty);

            // render
            glClearColor(Const::CLEAR_R, Const::CLEAR_G, Const::CLEAR_B, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


            // shadow pass
            // render all props into depth buffer from sun POV
            scene_shadow_pass(app.scene, app.obstacles, app.editor.cam_pos);
            glBindFramebuffer(GL_FRAMEBUFFER, app.scene.shadow_fbo);
            glViewport(0, 0, Const::SHADOW_MAP_SIZE, Const::SHADOW_MAP_SIZE);
            glClear(GL_DEPTH_BUFFER_BIT);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            editor_renderer_shadow_pass(app.editor_renderer, app.map,
                app.scene.light_space_mat, app.dynamic_sims);
            glCullFace(GL_BACK);
            glDisable(GL_CULL_FACE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT);

            // copy light data to editor_renderer for shadow sampling in draw_props
            app.editor_renderer.shadow_depth_tex = app.scene.shadow_depth_tex;
            app.editor_renderer.light_space_mat = app.scene.light_space_mat;



            scene_draw_sky(app.scene, view, proj);

            // draw world scene:
            // ground, gizmo
            // trike parked where it stopped last
            scene_draw(app.scene, app.trike, app.obstacles, view, proj, app.editor.show_hitboxes);

            // terrain wireframe
            if (app.editor.mode == MODE_TERRAIN || app.editor.mode == MODE_ROAD)
                editor_renderer_draw_terrain(app.editor_renderer, app.map.terrain, view, proj,
                app.editor.ghost_pos, app.editor.brush_radius, app.editor.placement_valid);

            // road splines
            editor_renderer_draw_roads(app.editor_renderer, app.map.roads, view, proj);

            // ocean
            editor_renderer_draw_ocean(app.editor_renderer, app.map.ocean, view, proj, dt,
            app.map.terrain.origin.x,
            app.map.terrain.origin.x + app.map.terrain.cols * app.map.terrain.cell_size,
            app.map.terrain.origin.z,
            app.map.terrain.origin.z + app.map.terrain.rows * app.map.terrain.cell_size);

             // solid terrain surface
            editor_renderer_draw_terrain_surface(app.editor_renderer, app.map.terrain, view, proj, app.map.ocean);


            // draw editor overlays:
            // grid, ghost, selection highlight
            editor_renderer_draw(app.editor_renderer, app.editor, app.map, view, proj, app.editor.show_hitboxes);

            // temp editor control hud
            font_draw(app.editor_renderer.font,"[TAB] drive  [L CLICK] place/select  [DEL] delete  [B] behavior  [Ctrl+S] save",
                      10, Const::WINDOW_HEIGHT - 40, 2, 0.7f, 0.7f, 0.7f);
            font_draw(app.editor_renderer.font,"[T] translate  [R] rotate  [Y] scale  [PgUp/PgDn] Y nudge  [1-9] prop  [/] page",
                      10, Const::WINDOW_HEIGHT - 20, 2, 0.7f, 0.7f, 0.7f);
            
            window_swap_buffers(app.window);
            window_poll_events();
            continue;
        }


        // DRIVING MODE
        TrikeInput input;
        input.throttle = (glfwGetKey(app.window.handle, GLFW_KEY_W) == GLFW_PRESS) ? 1.0f : 0.0f;
        bool s_held   = glfwGetKey(app.window.handle, GLFW_KEY_S) == GLFW_PRESS;
        input.brake   = (s_held && app.trike.speed >  0.5f) ? 1.0f : 0.0f;
        input.reverse = (s_held && app.trike.speed <= 0.5f) ? 1.0f : 0.0f;
        input.steer = 0.0f;
        if (glfwGetKey(app.window.handle, GLFW_KEY_A) == GLFW_PRESS) input.steer -= 1.0f;
        if (glfwGetKey(app.window.handle, GLFW_KEY_D) == GLFW_PRESS) input.steer += 1.0f;
        bool f_down = glfwGetKey(app.window.handle, GLFW_KEY_F) == GLFW_PRESS;
        if (f_down && !s_f_pressed_last) s_free_cam = !s_free_cam;
        s_f_pressed_last = f_down;

        // R key resets trike and dynamic objects to initial state
        static bool s_r_last = false;
        bool r_down = glfwGetKey(app.window.handle, GLFW_KEY_R) == GLFW_PRESS;
        if (r_down && !s_r_last){
            // reset trike to spawn
            app.trike = TrikeState{};
            s_cam_yaw   = Const::CAM_YAW_DEFAULT;
            s_cam_pitch = Const::CAM_PITCH_DEFAULT;
            s_cam_dist  = Const::CAM_DIST_DEFAULT;

            // reset all dynamic sims to their placed world positions
            app.dynamic_sims.clear();
            init_dynamic_sims(app);

            // clear all obstacle hit timers
            for (auto& obs : app.obstacles)
                obs.hit_timer = 0.0f;
        }
        s_r_last = r_down;

        // camera orbit input
        if (glfwGetKey(app.window.handle, GLFW_KEY_LEFT)  == GLFW_PRESS) s_cam_yaw -= Const::CAM_YAW_SPEED * dt;
        if (glfwGetKey(app.window.handle, GLFW_KEY_RIGHT) == GLFW_PRESS) s_cam_yaw += Const::CAM_YAW_SPEED * dt;
        if (glfwGetKey(app.window.handle, GLFW_KEY_UP)    == GLFW_PRESS) s_cam_pitch += Const::CAM_PITCH_SPEED * dt;
        if (glfwGetKey(app.window.handle, GLFW_KEY_DOWN)  == GLFW_PRESS) s_cam_pitch -= Const::CAM_PITCH_SPEED * dt;
        s_cam_pitch = glm::clamp(s_cam_pitch, Const::CAM_PITCH_MIN, Const::CAM_PITCH_MAX);
        // spring yaw offset back toward 0 (behind trike) when trike is moving
        if (std::abs(app.trike.speed) > 0.3f)
            s_cam_yaw = glm::mix(s_cam_yaw, 0.0f, 1.0f - std::exp(-3.5f * dt));

        // fixed timestep physics
        // physics will run constantly at 120 hz regardless if framerate is ass
        // accumulate real tim & consume it in fixed chunks
        // this prevents the sim from going insane in slow frames
        app.accumulator += dt;
        while (app.accumulator >= Const::FIXED_TIMESTEP){
            trike_physics_update(app.trike, input, app.map.terrain, Const::FIXED_TIMESTEP);
            app.accumulator -= Const::FIXED_TIMESTEP;
        }
        trike_model_update(app.scene.trike_model, app.trike.speed, dt);

        // update trike AABB after physics
        if (!app.trike.is_tipping && !app.trike.is_rolled_over)
            aabb_update(app.trike.aabb, app.trike.position, app.trike.heading);

        // tick impact timer
        if (app.trike.impact_timer > 0.0f) app.trike.impact_timer -= dt;

        // snapshot pre-collision speed so we can clamp after all responses
        float pre_collision_speed = app.trike.speed;
        bool any_collision = false;

        // collision detection + response
        // suspended during tumble
        if (app.trike.is_tipping || app.trike.is_rolled_over) goto skip_collision;
        for (auto& obs : app.obstacles){
            if (obs.hit_timer > 0.0f) obs.hit_timer -= dt;
            glm::vec3 to_obs = obs.position - app.trike.position;
            if (glm::dot(to_obs, to_obs) > 25.0f) continue;
            if (!aabb_overlap(app.trike.aabb, obs.aabb)) continue;
            // skip full response if we just hit this obstacle recently
            // MTV still separates, but no force injection until cooldown expires
            // I thought I fixed the impact collisionl loop
            // I'll add a per obs cooldown upon hit
            bool fresh_hit = (obs.hit_timer <= 0.0f);

            // snapshot speed before any response so we can cap the exit velocity
            // prevents restitution overshoot
            // aka the trike flying off max speed at angled collision hits
            float speed_before = std::abs(app.trike.speed);
            float lat_before = std::abs(app.trike.lateral_speed);

            // min translation vec
            // smallest possible push to separate 2 boxes
            // stoppable force (the trike) vs immovable object (the box) basically
            glm::vec3 mtv = aabb_mtv(app.trike.aabb, obs.aabb);
            app.trike.position += mtv;

            glm::vec3 mtv_normal = glm::length(mtv) > 0.0f
                ? glm::normalize(mtv) : glm::vec3(0.0f);

            glm::vec3 fwd = {std::cos(app.trike.heading), 0.0f, std::sin(app.trike.heading)};
            glm::vec3 rgt = {std::cos(app.trike.heading + glm::half_pi<float>()), 0.0f,
                             std::sin(app.trike.heading + glm::half_pi<float>())};

            float spd_dot = glm::dot(fwd, mtv_normal);
            float lat_dot = glm::dot(rgt, mtv_normal);
            float spd_along = app.trike.speed         * spd_dot;
            float lat_along = app.trike.lateral_speed * lat_dot;

            float closing = 0.0f;
            if (spd_along < 0.0f) closing += std::abs(spd_along);
            if (lat_along < 0.0f) closing += std::abs(lat_along);

            if (closing > 0.8f && fresh_hit){
                app.trike.last_impact_force = closing;
                app.trike.impact_timer = (closing > 2.0f) ? 0.35f : 0.0f;
                obs.hit_timer = 0.35f;

                if (spd_along < 0.0f)
                    app.trike.speed += (-spd_along) * (1.0f + Const::RESTITUTION) * spd_dot;
                if (lat_along < 0.0f)
                    app.trike.lateral_speed += (-lat_along) * (1.0f + Const::RESTITUTION) * lat_dot;

                float bleed = glm::clamp(closing * 0.06f, 0.05f, 0.4f);
                app.trike.speed *= (1.0f - bleed);
                app.trike.lateral_speed *= (1.0f - bleed);

                float side_factor = std::abs(lat_dot);
                if (side_factor > 0.3f && closing > 1.5f)
                    app.trike.roll_rate += side_factor * closing * 0.4f
                        * (lat_dot > 0.0f ? 1.0f : -1.0f);
            } 
            else {
                if (spd_along < 0.0f) app.trike.speed -= spd_along;
                if (lat_along < 0.0f) app.trike.lateral_speed -= lat_along;
            }
            glm::vec3 fwd2 = {std::cos(app.trike.heading), 0.0f, std::sin(app.trike.heading)};
            glm::vec3 rgt2 = {std::cos(app.trike.heading + glm::half_pi<float>()), 0.0f,
                             std::sin(app.trike.heading + glm::half_pi<float>())};
            // project full velocity and cancel the wall-normal component entirely
            // this prevents the trike re-entering the wall next tick
            glm::vec3 vel = fwd2 * app.trike.speed + rgt2 * app.trike.lateral_speed;
            float vel_into = glm::dot(vel, -mtv_normal);
            if (vel_into > 0.0f){
                glm::vec3 vel_corrected = vel + mtv_normal * vel_into;
                app.trike.speed = glm::dot(vel_corrected, fwd2);
                app.trike.lateral_speed = glm::dot(vel_corrected, rgt2);
            }
            // hard clamp: if still moving into wall after correction, zero that component
            float residual = glm::dot(
                fwd2 * app.trike.speed + rgt2 * app.trike.lateral_speed, -mtv_normal);
            if (residual > 0.0f){
                app.trike.speed -= residual * glm::dot(fwd2, -mtv_normal);
                app.trike.lateral_speed -= residual * glm::dot(rgt2, -mtv_normal);
            }
            app.trike.lateral_speed *= 0.55f;

            // cap exit velocity
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

        skip_collision:
        // clamp speed to pre-collision value after all responses
        // physics rebuilds speed inside the fixed timestep loop before we get here
        // so without this clamp, throttle held during wall contact gives free acceleration
        if (any_collision){
            float max_spd = std::abs(pre_collision_speed);
            if (std::abs(app.trike.speed) > max_spd + 0.5f) app.trike.speed = std::copysign(max_spd, app.trike.speed);
            app.trike.lateral_speed = 0.0f;
        }
            

        // DYNAMIC object integration + collision
        for (auto& [id, sim] : app.dynamic_sims){
            // find the world object for mass/restitution/friction
            const WorldObject* wo = nullptr;
            for (const auto& o : app.map.objects)
                if (o.id == id) { wo = &o; break; }
            if (!wo) continue;

            // rebuild AABB from current sim position
            // only awake objects need this every frame
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

                // mass ratio determines how much each party moves
                float total_mass = wo->mass + Const::TRIKE_MASS;
                float obj_share = Const::TRIKE_MASS / total_mass;
                float trike_share = wo->mass / total_mass;

                // separate: trike gets pushed back, object gets pushed forward
                app.trike.position -= mtv * trike_share;
                sim.position += mtv * obj_share;

                // closing speed along hit normal
                glm::vec3 trike_fwd = { std::cos(app.trike.heading), 0.0f, std::sin(app.trike.heading) };
                float closing = std::abs(app.trike.speed) * std::abs(glm::dot(trike_fwd, -hit_normal));

                // impulse to object
                // linear push in hit direction
                float impulse = closing * (1.0f + wo->restitution) * (Const::TRIKE_MASS / total_mass);
                sim.velocity += -hit_normal * impulse;

                // torque 
                // pitch and roll from hit direction and object height
                float height = (sim.aabb.max.y - sim.aabb.min.y);
                float inertia = wo->mass * height * height * 0.3f; // approx rod inertia
                float torque_mag = impulse * height * 0.5f / (inertia + 0.001f);

                // hit from front/back -> pitch
                sim.pitch_vel += torque_mag * glm::dot(-hit_normal, trike_fwd);
                // hit from side → roll
                glm::vec3 trike_rgt = { std::cos(app.trike.heading + glm::half_pi<float>()), 0.0f,
                                        std::sin(app.trike.heading + glm::half_pi<float>()) };
                sim.roll_vel  += torque_mag * glm::dot(-hit_normal, trike_rgt);

                // spin on Y
                // glancing cause yaw spin
                sim.yaw_vel   += torque_mag * 0.4f * (glm::dot(-hit_normal, trike_rgt));

                // trike loses speed proportional to object mass
                app.trike.speed *= (1.0f - glm::clamp(wo->mass / total_mass * 0.6f, 0.0f, 0.6f));

                // flash + cam shake scaled to closing speed same as static hits
                if (closing > 0.8f){
                    sim.hit_timer = glm::clamp(closing * 0.12f, 0.15f, 0.45f);
                    if (closing > 2.0f){
                        app.trike.last_impact_force = closing;
                        app.trike.impact_timer = glm::clamp(closing * 0.08f, 0.15f, 0.35f);
                    }
                }

                aabb_update(app.trike.aabb, app.trike.position, app.trike.heading);
            }

            if (sim.sleeping) continue;

            // integrate position
            sim.position += sim.velocity * dt;

            // snap dyn objects to terrain
            float sim_ground = heightfield_sample(app.map.terrain, sim.position.x, sim.position.z);
            if (sim.position.y < sim_ground) sim.position.y = sim_ground;

            // ground friction bleeds linear velocity
            float drag = 1.0f - wo->friction * dt;
            sim.velocity *= glm::clamp(drag, 0.0f, 1.0f);

            // integrate angular
            sim.yaw += sim.yaw_vel * dt;
            sim.pitch += sim.pitch_vel * dt;
            sim.roll += sim.roll_vel  * dt;

            // angular drag
            float ang_drag = 1.0f - wo->friction * 1.5f * dt;
            ang_drag = glm::clamp(ang_drag, 0.0f, 1.0f);
            sim.yaw_vel   *= ang_drag;
            sim.pitch_vel *= ang_drag;
            sim.roll_vel  *= ang_drag;

            // gravity pulls the tip down while falling
            // only apply while not yet flat (under 90 degrees)
            float gravity_torque = Const::GRAVITY * 0.4f;
            if (std::abs(sim.pitch) > 0.05f && std::abs(sim.pitch) < glm::half_pi<float>())
                sim.pitch_vel += std::copysign(gravity_torque, sim.pitch) * dt;
            if (std::abs(sim.roll) > 0.05f && std::abs(sim.roll) < glm::half_pi<float>())
                sim.roll_vel  += std::copysign(gravity_torque, sim.roll)  * dt;

            // once past 90 degrees the object is on the ground
            // snap flat and kill angular
            if (std::abs(sim.pitch) >= glm::half_pi<float>()){
                sim.pitch = std::copysign(glm::half_pi<float>(), sim.pitch);
                sim.pitch_vel = 0.0f;
            }
            if (std::abs(sim.roll) >= glm::half_pi<float>()){
                sim.roll = std::copysign(glm::half_pi<float>(), sim.roll);
                sim.roll_vel  = 0.0f;
            }

            // tick flash timer
            if (sim.hit_timer > 0.0f) sim.hit_timer -= dt;

            float lin_spd = glm::length(sim.velocity);
            float ang_spd = std::abs(sim.yaw_vel) + std::abs(sim.pitch_vel) + std::abs(sim.roll_vel);
            if (lin_spd < 0.05f && ang_spd < 0.02f){
                sim.velocity  = glm::vec3(0.0f);
                sim.yaw_vel   = sim.pitch_vel = sim.roll_vel = 0.0f;
                sim.sleeping  = true;
            }
        }

        // dynamic vs dynamic collision
        // O(n^2) fine for small barangay object counts
        for (auto& [id_a, sim_a] : app.dynamic_sims){
            if (sim_a.sleeping) continue;
            for (auto& [id_b, sim_b] : app.dynamic_sims){
                if (id_a >= id_b) continue; // skip self and already-checked pairs
                if (!aabb_overlap(sim_a.aabb, sim_b.aabb)) continue;

                const WorldObject* wo_a = nullptr;
                const WorldObject* wo_b = nullptr;
                for (const auto& o : app.map.objects){
                    if (o.id == id_a) wo_a = &o;
                    if (o.id == id_b) wo_b = &o;
                }
                if (!wo_a || !wo_b) continue;

                sim_b.sleeping = false;

                glm::vec3 mtv = aabb_mtv(sim_a.aabb, sim_b.aabb);
                glm::vec3 hit_normal = glm::length(mtv) > 0.0f ? glm::normalize(mtv) : glm::vec3(0,0,1);

                float total_mass = wo_a->mass + wo_b->mass;
                sim_a.position -= mtv * (wo_b->mass / total_mass);
                sim_b.position += mtv * (wo_a->mass / total_mass);

                // exchange velocity scaled by mass — simple elastic collision
                float restitution = (wo_a->restitution + wo_b->restitution) * 0.5f;
                float rel_vel = glm::dot(sim_a.velocity - sim_b.velocity, hit_normal);
                if (rel_vel > 0.0f){
                    float impulse = rel_vel * (1.0f + restitution) / (1.0f/wo_a->mass + 1.0f/wo_b->mass);
                    sim_a.velocity -= hit_normal * (impulse / wo_a->mass);
                    sim_b.velocity += hit_normal * (impulse / wo_b->mass);

                    // transfer some angular from linear impulse
                    float torque = impulse * 0.3f;
                    sim_a.roll_vel -= torque / (wo_a->mass + 0.001f);
                    sim_b.roll_vel += torque / (wo_b->mass + 0.001f);
                }
            }
        }

        // camera
        float yaw_r = glm::radians(s_cam_yaw);
        float cam_yaw_world = app.trike.heading + yaw_r + glm::radians(180.0f);

        float speed_t = glm::clamp(std::abs(app.trike.speed) / Const::TRIKE_MAX_SPEED, 0.0f, 1.0f);
        float slope_contribution = -app.trike.pitch_angle * speed_t * Const::CAM_SLOPE_PITCH_SCALE;
        static float s_cam_pitch_smoothed = 0.0f;
        s_cam_pitch_smoothed = glm::mix(s_cam_pitch_smoothed, slope_contribution,
            glm::clamp(Const::CAM_SLOPE_LERP_SPEED * dt, 0.0f, 1.0f));

        float pitch_r = glm::radians(s_cam_pitch) + s_cam_pitch_smoothed;
        pitch_r = glm::clamp(pitch_r, glm::radians(Const::CAM_PITCH_MIN), glm::radians(Const::CAM_PITCH_MAX + 25.0f));

        // smooth lookat height bias
        static float s_target_y_bias = 0.0f;
        float target_y_goal = -app.trike.pitch_angle * speed_t * Const::CAM_SLOPE_TARGET_Y_BIAS;
        s_target_y_bias = glm::mix(s_target_y_bias, target_y_goal,
            glm::clamp(Const::CAM_SLOPE_LERP_SPEED * dt, 0.0f, 1.0f));

        glm::vec3 cam_origin = app.trike.position + glm::vec3(0.0f, Const::CAM_ORBIT_TARGET_Y, 0.0f);
        glm::vec3 ideal_eye  = cam_origin + glm::vec3(
            s_cam_dist * cosf(pitch_r) * cosf(cam_yaw_world),
            s_cam_dist * sinf(pitch_r),
            s_cam_dist * cosf(pitch_r) * sinf(cam_yaw_world));
        s_cam_pos = glm::mix(s_cam_pos, ideal_eye, Const::CAM_LERP_SPEED * dt);

        float fwd_angle = app.trike.heading;
        glm::vec3 fwd = glm::vec3(cosf(fwd_angle), 0.0f, sinf(fwd_angle));
        float lookahead = (app.trike.speed / Const::TRIKE_MAX_SPEED) * Const::CAM_LOOKAHEAD;
        glm::vec3 target = cam_origin + fwd * lookahead + glm::vec3(0.0f, s_target_y_bias, 0.0f);

        // camera shake on impact
        // shake the lookat target and not the eye position
        // this feels more natural and looks more natural
        // 3 sin.cos at diff frequencies gives pseudorandom wobble without actual RNG
        // decays at zero as impact_timer runs out
        // pretty sweet if i must say
        if (app.trike.impact_timer > 0.0f){
            float t = app.trike.impact_timer;
            float mag = glm::clamp(app.trike.last_impact_force * 0.018f, 0.0f, 0.4f);
            float decay = t / 0.35f;
            target.x += std::sin(t * 47.0f) * mag * decay;
            target.y += std::cos(t * 31.0f) * mag * decay;
            target.z += std::sin(t * 23.0f) * mag * decay;
        }

        glm::mat4 view;
        if (s_free_cam){
            glm::vec3 top = app.trike.position + glm::vec3(0.0f, 15.0f, 0.0f);
            view = glm::lookAt(top, app.trike.position, glm::vec3(1,0,0));
        } 
        else {
            view = glm::lookAt(s_cam_pos, target, glm::vec3(0,1,0));
        }

        glm::mat4 proj = glm::perspective(
            glm::radians(Const::CAM_FOV),
            (float)Const::WINDOW_WIDTH / (float)Const::WINDOW_HEIGHT,
            Const::CAM_NEAR, Const::CAM_FAR);

        // render
        glClearColor(Const::CLEAR_R, Const::CLEAR_G, Const::CLEAR_B, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        
        // shadow pass
        // render all props into depth buffer from sun POV
        scene_shadow_pass(app.scene, app.obstacles, app.editor.cam_pos);
        glBindFramebuffer(GL_FRAMEBUFFER, app.scene.shadow_fbo);
        glViewport(0, 0, Const::SHADOW_MAP_SIZE, Const::SHADOW_MAP_SIZE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        editor_renderer_shadow_pass(app.editor_renderer, app.map,
                app.scene.light_space_mat, app.dynamic_sims);
        glCullFace(GL_BACK);
        glDisable(GL_CULL_FACE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT);

        // copy light data to editor_renderer for shadow sampling in draw_props
        app.editor_renderer.shadow_depth_tex = app.scene.shadow_depth_tex;
        app.editor_renderer.light_space_mat = app.scene.light_space_mat;



        scene_draw_sky(app.scene, view, proj);

        // entire render pass in one call
        // ground, gizmo, trike, obstacles, wireframes
        scene_draw(app.scene, app.trike, app.obstacles, view, proj, app.editor.show_hitboxes);

        // build flash map from current obstacle hit timers
        std::map<int,float> flash_map;
        for (const auto& obs : app.obstacles)
            if (obs.world_id != -1 && obs.hit_timer > 0.0f)
                flash_map[obs.world_id] = obs.hit_timer;
        for (const auto& [id, sim] : app.dynamic_sims)
            if (sim.hit_timer > 0.0f)
                flash_map[id] = sim.hit_timer;

        editor_renderer_draw_roads(app.editor_renderer, app.map.roads, view, proj);
        editor_renderer_draw_ocean(app.editor_renderer, app.map.ocean, view, proj, dt,
            app.map.terrain.origin.x,
            app.map.terrain.origin.x + app.map.terrain.cols * app.map.terrain.cell_size,
            app.map.terrain.origin.z,
            app.map.terrain.origin.z + app.map.terrain.rows * app.map.terrain.cell_size);
        editor_renderer_draw_terrain_surface(app.editor_renderer, app.map.terrain, view, proj, app.map.ocean);
        editor_renderer_draw_props(app.editor_renderer, app.map, view, proj, flash_map, app.dynamic_sims);
        hud_draw(app.hud, app.trike);
        window_swap_buffers(app.window);
        window_poll_events();
    }
}

void app_shutdown(App& app){
    hud_destroy(app.hud);
    scene_destroy(app.scene);
    window_destroy(app.window);
    world_map_save(app.map, Const::MAP_SAVE_PATH);
    editor_renderer_destroy(app.editor_renderer);
    app.running = false;
}