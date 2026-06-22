#include "../renderer/editor_renderer.hpp"
#include "../physics/trike_aabb.hpp"
#include "../physics/collision.hpp"
#include "../world/world_map.hpp"
#include "../world/road_builder.hpp"
#include "../world/ambience_zone.hpp"
#include "../world/rain.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "app.hpp"
#include "const.hpp"
#include "settings.hpp"
#include "editor_cam.hpp"
#include "editor_input.hpp"
#include "npc_update.hpp"
#include "cam.hpp"
#include <cmath>
#include <iostream>
#include <fstream>
#include <filesystem>

/*****************************************************
 MAIN APPLICATION - init, loop, shutdown
 subsystem calls logic lives in:
   collision.cpp  - static + dynamic collision
   npc_update.cpp - NPC per-frame logic
   cam.cpp        - camera computation
******************************************************/

static CamState s_cam = {
    Const::CAM_YAW_DEFAULT,
    Const::CAM_PITCH_DEFAULT,
    Const::CAM_DIST_DEFAULT,
};

// map editor toggle
// edge trigger so a single tab press flips mode once
static bool s_tab_pressed_last = false;

// RIGID BODY DYNAMICS INIT
// builds initial physics state for all DYNAMIC objects from their placed world positions
// skips objects already in the sim map so editor->drive transitions are non-destructive
static void init_dynamic_sims(App& app){
    for (const auto& o : app.map.objects){
        if (o.behavior != DYNAMIC) continue;
        if (app.dynamic_sims.count(o.id)) continue;

        DynamicSim sim;
        sim.position = o.position;
        sim.yaw      = 0.0f;

        auto bit = app.editor_renderer.prop_bounds.find(o.model_path);
        if (bit != app.editor_renderer.prop_bounds.end()){
            float yoff = app.editor_renderer.prop_y_offset.count(o.model_path)
                ? app.editor_renderer.prop_y_offset.at(o.model_path) : 0.0f;
            glm::vec3 lmin = bit->second.local_min;
            glm::vec3 lmax = bit->second.local_max;
            glm::vec3 smin = { lmin.x*o.scale.x, (lmin.y+yoff)*o.scale.y, lmin.z*o.scale.z };
            glm::vec3 smax = { lmax.x*o.scale.x, (lmax.y+yoff)*o.scale.y, lmax.z*o.scale.z };
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
        else {
            glm::vec3 half = o.scale * 0.5f;
            sim.aabb = { sim.position - half, sim.position + half };
        }
        app.dynamic_sims[o.id] = sim;
    }
}

/********************************************
 APP_INIT
 subsystem init order:
 1. window + gl
 2. scene, editor, road_builder, terrain
 3. prop bounds
 4. obstacles, dynamic rigid body, npcs
 5. id lookup
 6. driver pose, hud, audio
 7. cam seed
********************************************/
void app_init(App& app){
    window_init(app.window, Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT, Const::WINDOW_TITLE);

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
        glm::vec3(-(Const::TERRAIN_COLS * Const::TERRAIN_CELL_SIZE) * 0.5f, 0.0f,
                  -(Const::TERRAIN_ROWS * Const::TERRAIN_CELL_SIZE) * 0.5f));

    editor_scan_props(app.editor, "../assets");

    world_map_load(app.map, Const::MAP_SAVE_PATH);
    ambience_load(app.map.ambience_zones, app.map.ambience_count,
        Const::MAX_AMBIENCE_ZONES, Const::AMBIENCE_SAVE_PATH);

    for (auto& r : app.map.roads)
        road_spline_build_mesh(r, &app.map.terrain);

    // trigger prop bound caching before building obstacles
    for (const auto& o : app.map.objects){
        if (!o.model_path.empty())
            editor_get_y_floor_offset(app.editor_renderer, o.model_path);
    }
    world_map_to_obstacles(app);
    init_dynamic_sims(app);
    init_npcs(app);

    app.wo_by_id.clear();
    for (const auto& o : app.map.objects)
        app.wo_by_id[o.id] = &o;

    editor_renderer_preload_textures(app.editor_renderer);

    // load saved driver pose so drive mode is correct from the start
    {
        std::ifstream pf("../assets/entity/driver_pose.txt");
        if (pf.is_open()){
            std::string tag;
            while (pf >> tag){
                if (tag == "seat"){
                    pf >> app.editor.pose_seat.x >> app.editor.pose_seat.y >> app.editor.pose_seat.z;
                }
                else if (tag == "quat"){
                    int idx; pf >> idx;
                    if (idx >= 0 && idx < 6)
                        pf >> app.editor.pose_quat[idx].w >> app.editor.pose_quat[idx].x
                           >> app.editor.pose_quat[idx].y >> app.editor.pose_quat[idx].z;
                }
                else if (tag == "offset"){
                    int idx; pf >> idx;
                    if (idx >= 0 && idx < 6)
                        pf >> app.editor.pose_offset[idx].x >> app.editor.pose_offset[idx].y
                           >> app.editor.pose_offset[idx].z;
                }
            }
            std::cout << "[app] loaded driver pose from driver_pose.txt\n";
        }
    }

    hud_init(app.hud, Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT);
    audio_init(app.audio, "../assets");
    settings_load();
    rain_init(app.rain, app.trike.position);

    s_cam.pitch = Const::CAM_PITCH_DEFAULT;
    s_cam.dist  = Const::CAM_DIST_DEFAULT;
    cam_seed(s_cam, app);

    app.last_time   = (float)glfwGetTime();
    app.accumulator = 0.0f;
    app.running     = true;

    window_show(app.window);
}

void editor_input_settings(EditorState& editor, GLFWwindow* window);

/********************************************
 MAIN LOOP
********************************************/
void app_run(App& app){
    while (!window_should_close(app.window)){

        // delta time clamped so a slow frame or debugger pause doesn't explode physics
        float now = (float)glfwGetTime();
        float dt  = glm::min(now - app.last_time, Const::MAX_DELTA);
        app.last_time = now;

        /******************************************************************************
         GLOBAL TOGGLE KEYS run in both EDITOR and DRIVE mode
        ******************************************************************************/

        // H key show AABB hitboxes (ctrl+H reserved for editor pose mode)
        static bool s_h_last = false;
        bool h_down  = glfwGetKey(app.window.handle, GLFW_KEY_H) == GLFW_PRESS;
        bool ctrl_h  = glfwGetKey(app.window.handle, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
        if (!app.editor.settings_open && h_down && !s_h_last && !ctrl_h)
            app.editor.show_hitboxes = !app.editor.show_hitboxes;
        s_h_last = h_down;

        // TAB key toggle EDITOR <-> DRIVE (blocked in Audio submode and settings)
        bool tab_down = glfwGetKey(app.window.handle, GLFW_KEY_TAB) == GLFW_PRESS;
        if (!app.editor.settings_open && tab_down && !s_tab_pressed_last && app.editor.mode != MODE_AUDIO){
            app.editor.active = !app.editor.active;
            if (app.editor.active){
                app.editor.cam_pos = app.trike.position + glm::vec3(0.0f, 12.0f, 0.0f);
                editor_cam_init(app.window.handle);
            }
            else {
                // only rebuild if map changed since last switch
                if (app.obstacles_dirty){
                    world_map_to_obstacles(app);
                    init_dynamic_sims(app);
                    init_npcs(app);
                    app.wo_by_id.clear();
                    for (const auto& o : app.map.objects)
                        app.wo_by_id[o.id] = &o;
                }
            }
        }
        s_tab_pressed_last = tab_down;

        // ESC - settings menu toggle
        static bool s_esc_last = false;
        bool esc_down = glfwGetKey(app.window.handle, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (esc_down && !s_esc_last){
            app.editor.settings_open = !app.editor.settings_open;
            app.editor.settings_page = SETTINGS_PAGE_MAIN;
            app.editor.settings_cursor = 0;
            if (app.editor.settings_open)
                audio_pause(app.audio);
            else {
                audio_resume(app.audio);
                settings_save();
                scene_shadow_resize(app.scene);
            }

        }
        s_esc_last = esc_down;

        /******************************************************************************
         EDITOR MODE
        ******************************************************************************/
        if (app.editor.active){
            if (!app.editor.settings_open)
                editor_cam_update(app.editor, app.window.handle, dt);

            glClearColor(Const::CLEAR_R, Const::CLEAR_G, Const::CLEAR_B, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glm::mat4 view = editor_cam_get_view(app.editor);
            glm::mat4 proj = glm::perspective(
                glm::radians(Const::CAM_FOV),
                (float)Const::WINDOW_WIDTH / (float)Const::WINDOW_HEIGHT,
                Const::CAM_NEAR, Const::CAM_FAR);

            if (!app.editor.settings_open)
                editor_input_update(app.editor, app.map, app.editor_renderer,
                    app.window.handle, view, proj,
                    Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT, dt, app.obstacles_dirty);

            // SHADOW PASS
            scene_update_daytime(app.scene, dt);
            app.editor_renderer.sun_dir = app.scene.sun_dir;
            app.editor_renderer.light_color = app.scene.light_color;
            app.editor_renderer.ambient = app.scene.ambient;
            app.editor_renderer.diff_intensity = app.scene.diff_intensity;
            app.editor_renderer.shadow_cull_center = app.editor.cam_pos;
            scene_shadow_pass(app.scene, app.obstacles, app.editor.cam_pos);

            if (my_settings.render_shadows){
                glBindFramebuffer(GL_FRAMEBUFFER, app.scene.shadow_fbo);
                glViewport(0, 0, my_settings.shadow_map_size, my_settings.shadow_map_size);
                glClear(GL_DEPTH_BUFFER_BIT);
                glEnable(GL_CULL_FACE);
                glCullFace(GL_FRONT);
                editor_renderer_shadow_pass(app.editor_renderer, app.map, app.scene.light_space_mat, app.dynamic_sims);
                scene_trike_shadow_draw(app.scene, app.trike);
                driver_model_draw(app.scene.driver_model, app.player, app.trike,
                    app.scene.shadow_shader, app.scene.light_space_mat, glm::mat4(1.0f),
                    app.editor.pose_quat, app.editor.pose_offset, app.editor.pose_seat);
                glCullFace(GL_BACK);
                glDisable(GL_CULL_FACE);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            else {
                glBindFramebuffer(GL_FRAMEBUFFER, app.scene.shadow_fbo);
                glViewport(0, 0, my_settings.shadow_map_size, my_settings.shadow_map_size);
                glClearDepth(1.0);
                glClear(GL_DEPTH_BUFFER_BIT);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            {
                int fb_w, fb_h;
                glfwGetFramebufferSize(app.window.handle, &fb_w, &fb_h);
                glViewport(0, 0, fb_w, fb_h);
            }

            // push scene lighting to editor_renderer
            app.editor_renderer.shadow_depth_tex = app.scene.shadow_depth_tex;
            app.editor_renderer.light_space_mat = app.scene.light_space_mat;
            app.editor_renderer.night_factor = app.scene.night_factor;
            app.editor_renderer.fog_color = app.scene.fog_color;
            app.editor_renderer.fog_near = app.scene.fog_near;
            app.editor_renderer.fog_far = app.scene.fog_far;
            // -1 hides pose overlay unless we're actively in pose mode
            app.editor_renderer.pose_npc_id = (app.editor.mode == MODE_POSE) ? app.editor.pose_npc_id : -1;

            // MAIN DRAW
            scene_draw_sky(app.scene, view, proj);
            scene_draw(app.scene, app.trike, app.obstacles, app.map.lights, view, proj,
                app.editor.show_hitboxes);

            // driver visible in editor except pose mode (pose mode has its own isolated draw)
            if (app.editor.mode != MODE_POSE){
                PlayerState fake_driving;
                fake_driving.mode = PLAYER_DRIVING;
                scene_draw_driver(app.scene, fake_driving, app.trike, view, proj,
                    app.editor_renderer.obj_shader,
                    app.editor.pose_quat, app.editor.pose_offset, app.editor.pose_seat);
            }

            if (app.editor.mode == MODE_TERRAIN || app.editor.mode == MODE_ROAD)
                editor_renderer_draw_terrain(app.editor_renderer, app.map.terrain, view, proj,
                    app.editor.ghost_pos, app.editor.brush_radius, app.editor.placement_valid);

            editor_renderer_draw_roads(app.editor_renderer, app.map.roads, view, proj);
            editor_renderer_draw_ocean(app.editor_renderer, app.map.ocean, view, proj, dt,
                app.map.terrain.origin.x,
                app.map.terrain.origin.x + app.map.terrain.cols * app.map.terrain.cell_size,
                app.map.terrain.origin.z,
                app.map.terrain.origin.z + app.map.terrain.rows * app.map.terrain.cell_size);
            editor_renderer_draw_terrain_surface(app.editor_renderer, app.map.terrain, view, proj,
                app.map.ocean);
            editor_renderer_draw(app.editor_renderer, app.editor, app.map, view, proj,
                app.editor.show_hitboxes, app.map.lights);

            if (app.editor.mode == MODE_POSE){
                DriverModel* pose_npc_model = nullptr;
                for (const auto& o : app.map.objects){
                    if (o.id != app.editor.pose_npc_id) continue;
                    auto mit = app.npc_model_cache.find(o.model_path);
                    if (mit != app.npc_model_cache.end())
                        pose_npc_model = &mit->second;
                    break;
                }
                editor_renderer_draw_pose_mode(app.editor_renderer, app.editor,
                    app.scene.driver_model, app.scene.trike_model, view, proj,
                    pose_npc_model, app.map);
            }

            if (!app.editor.settings_open){
                editor_renderer_draw_hud(app.editor_renderer, app.editor, app.map);
                font_draw(app.editor_renderer.font,
                    "[TAB] drive  [L CLICK] place/select  [DEL] delete  [B] behavior  [Ctrl+S] save",
                    10, Const::WINDOW_HEIGHT - 40, 2, 0.7f, 0.7f, 0.7f);
                font_draw(app.editor_renderer.font,
                    "[T] translate  [R] rotate  [Y] scale  [PgUp/PgDn] Y nudge  [1-9] prop  [/] page",
                    10, Const::WINDOW_HEIGHT - 20, 2, 0.7f, 0.7f, 0.7f);
            }
            if (app.editor.settings_open){
                editor_input_settings(app.editor, app.window.handle);
                editor_renderer_draw_settings_menu(app.editor_renderer, app.editor);
            }

            // pedestrian config overlay for selected object
            if (!app.editor.settings_open && app.editor.selected_id != -1){
                for (const auto& o : app.map.objects){
                    if (o.id != app.editor.selected_id) continue;
                    if (o.behavior != PEDESTRIAN) break;
                    char buf[256];
                    snprintf(buf, sizeof(buf),
                        "[PEDESTRIAN] type:%s  hail:%s  weight:%.1fkg  [J]=type [G]=hail [+/-]=weight",
                        NPC_TYPE_NAMES[o.npc_type], o.npc_can_hail ? "YES" : "NO", o.npc_weight);
                    font_draw(app.editor_renderer.font, buf,
                        10, Const::WINDOW_HEIGHT - 60, 2, 1.0f, 0.85f, 0.3f);
                    snprintf(buf, sizeof(buf),
                        "walk_a:(%.1f,%.1f)  walk_b:(%.1f,%.1f)  drop:(%.1f,%.1f)  [I]=A [U]=B [X]=drop",
                        o.npc_walk_a.x, o.npc_walk_a.z,
                        o.npc_walk_b.x, o.npc_walk_b.z,
                        o.npc_drop_point.x, o.npc_drop_point.z);
                    font_draw(app.editor_renderer.font, buf,
                        10, Const::WINDOW_HEIGHT - 80, 2, 1.0f, 0.85f, 0.3f);
                    break;
                }
            }

            window_swap_buffers(app.window);
            window_poll_events();
            continue;
        }

        /******************************************************************************
         DRIVE MODE
        ******************************************************************************/

        glm::mat4 proj = glm::perspective(
            glm::radians(Const::CAM_FOV),
            (float)Const::WINDOW_WIDTH / (float)Const::WINDOW_HEIGHT,
            Const::CAM_NEAR, Const::CAM_FAR);
        glm::mat4 view = cam_update(s_cam, app, dt, false);
        

        // gate gameplay to freeze when opening settings
        if (!app.editor.settings_open){
            // TRIKE INPUT
            // S: brake if moving forward, reverse if stopped
            TrikeInput input = {};
            if (app.player.mode == PLAYER_DRIVING || app.player.mode == PLAYER_MOUNTING){
                input.throttle = (glfwGetKey(app.window.handle, GLFW_KEY_W) == GLFW_PRESS) ? 1.0f : 0.0f;
                bool s_held   = glfwGetKey(app.window.handle, GLFW_KEY_S) == GLFW_PRESS;
                input.brake   = (s_held && app.trike.speed >  0.5f) ? 1.0f : 0.0f;
                input.reverse = (s_held && app.trike.speed <= 0.5f) ? 1.0f : 0.0f;
                input.steer   = 0.0f;
                if (glfwGetKey(app.window.handle, GLFW_KEY_A) == GLFW_PRESS) input.steer -= 1.0f;
                if (glfwGetKey(app.window.handle, GLFW_KEY_D) == GLFW_PRESS) input.steer += 1.0f;
            }
            else {
                // FOOT MODE - A/D orbit camera, W/S move along cam facing
                float move   = 0.0f;
                bool a_held  = glfwGetKey(app.window.handle, GLFW_KEY_A) == GLFW_PRESS;
                bool d_held  = glfwGetKey(app.window.handle, GLFW_KEY_D) == GLFW_PRESS;
                if (glfwGetKey(app.window.handle, GLFW_KEY_W) == GLFW_PRESS) move =  1.0f;
                if (glfwGetKey(app.window.handle, GLFW_KEY_S) == GLFW_PRESS) move = -1.0f;
                if (a_held) s_cam.yaw -= 120.0f * dt;
                if (d_held) s_cam.yaw += 120.0f * dt;

                // +180 so W moves toward where the camera is looking
                float cam_world_angle = glm::radians(s_cam.yaw) + glm::radians(180.0f);
                glm::vec3 fwd_dir = { std::cos(cam_world_angle), 0.0f, std::sin(cam_world_angle) };

                float walk_speed = 4.0f;
                glm::vec3 walk_vel = glm::vec3(0.0f);
                if (move != 0.0f){
                    walk_vel = fwd_dir * (move * walk_speed);
                    app.player.yaw = std::atan2(walk_vel.z, walk_vel.x);
                }

                // wrap yaw diff to [-pi, pi] to always take the short arc
                float yaw_diff = app.player.yaw - app.player.visual_yaw;
                while (yaw_diff >  glm::pi<float>()) yaw_diff -= glm::two_pi<float>();
                while (yaw_diff < -glm::pi<float>()) yaw_diff += glm::two_pi<float>();
                app.player.visual_yaw += yaw_diff * glm::clamp(12.0f * dt, 0.0f, 1.0f);

                app.player.pos += walk_vel * dt;
                app.player.speed = glm::length(walk_vel);
                app.player.pos.y = heightfield_sample(app.map.terrain, app.player.pos.x, app.player.pos.z);
                app.player.anim_timer += (app.player.speed > 0.1f ? app.player.speed * 1.8f : 1.0f) * dt;

                // footstep SFX mapped to surface type
                if (app.player.speed > 0.1f){
                    static const char* STEP_PATHS[(int)SURFACE_COUNT] = {
                        "",
                        "audio/footstep/concrete",
                        "audio/footstep/gravel",
                        "audio/footstep/dirt",
                        "audio/footstep/sand",
                        "audio/footstep/grass",
                        "audio/footstep/concrete",
                        "audio/footstep/rock"
                    };
                    SurfaceType surf = heightfield_get_surface(app.map.terrain,
                        app.player.pos.x, app.player.pos.z);
                    int si = glm::clamp((int)surf, 0, (int)SURFACE_COUNT - 1);
                    if (si > 0)
                        audio_trigger_step(app.audio,
                            std::string("../assets/") + STEP_PATHS[si], app.player.speed * dt);
                }
            }

            // F key - free cam
            static bool s_f_pressed_last = false;
            bool f_down = glfwGetKey(app.window.handle, GLFW_KEY_F) == GLFW_PRESS;
            if (f_down && !s_f_pressed_last) s_cam.free_cam = !s_cam.free_cam;
            s_f_pressed_last = f_down;

            // Q key -  confirm passenger pickup
            static bool s_q_last = false;
            bool q_down    = glfwGetKey(app.window.handle, GLFW_KEY_Q) == GLFW_PRESS;
            bool s_q_pickup = (q_down && !s_q_last);
            s_q_last = q_down;

            // E  key - mount/dismount
            static bool s_e_last = false;
            bool e_down = glfwGetKey(app.window.handle, GLFW_KEY_E) == GLFW_PRESS;
            if (e_down && !s_e_last && app.player.mode != PLAYER_MOUNTING){
                if (app.player.mode == PLAYER_DRIVING){
                    float side = app.trike.heading + glm::half_pi<float>();
                    app.player.pos = app.trike.position + glm::vec3(std::cos(side), 0.0f, std::sin(side)) * 1.2f;
                    app.player.yaw = app.trike.heading;
                    app.player.visual_yaw = app.trike.heading;
                    s_cam.yaw = 0.0f;
                    s_cam.pos = app.player.pos + glm::vec3(0.0f, 4.0f, 0.0f);
                    s_cam.needs_snap = true;
                    app.player.mode = PLAYER_FOOT;
                    app.player.headlights_on = false;
                    if (app.audio.radio_on) audio_radio_toggle(app.audio);
                }
                else {
                    // 3m mount radius, stored as squared distance to avoid sqrt
                    glm::vec3 delta = app.trike.position - app.player.pos;
                    delta.y = 0.0f;
                    if (glm::dot(delta, delta) < 9.0f){
                        app.player.mode        = PLAYER_MOUNTING;
                        app.player.mount_timer = 0.3f;
                    }
                }
            }
            s_e_last = e_down;

            // brief mount transition so the driver snap doesn't look instant
            if (app.player.mode == PLAYER_MOUNTING){
                app.player.mount_timer -= dt;
                if (app.player.mount_timer <= 0.0f)
                    app.player.mode = PLAYER_DRIVING;
            }

            // R key - full reset: player, dynamic objects, cam
            static bool s_r_last = false;
            bool r_down = glfwGetKey(app.window.handle, GLFW_KEY_R) == GLFW_PRESS;
            if (r_down && !s_r_last){
                app.trike = TrikeState{};
                app.player = PlayerState{};
                s_cam.yaw = Const::CAM_YAW_DEFAULT;
                s_cam.pitch = Const::CAM_PITCH_DEFAULT;
                s_cam.dist = Const::CAM_DIST_DEFAULT;
                app.dynamic_sims.clear();
                init_dynamic_sims(app);
                for (auto& obs : app.obstacles) obs.hit_timer = 0.0f;
            }
            s_r_last = r_down;

            // L key - headlight toggle (driving only)
            static bool s_l_last = false;
            bool l_down = glfwGetKey(app.window.handle, GLFW_KEY_L) == GLFW_PRESS;
            if (l_down && !s_l_last && app.player.mode == PLAYER_DRIVING){
                app.player.headlights_on = !app.player.headlights_on;
                audio_trigger_voice_local(app.audio, "../assets/audio/misc/headlight_switch.wav");
            }
            s_l_last = l_down;

            // P key - radio toggle (driving only)
            static bool s_p_last = false;
            bool p_down = glfwGetKey(app.window.handle, GLFW_KEY_P) == GLFW_PRESS;
            if (p_down && !s_p_last && app.player.mode == PLAYER_DRIVING)
                audio_radio_toggle(app.audio);
            s_p_last = p_down;

            // / key - next radio track
            static bool s_next_last = false;
            bool next_down = glfwGetKey(app.window.handle, GLFW_KEY_SLASH) == GLFW_PRESS;
            if (next_down && !s_next_last && app.audio.radio_on)
                audio_radio_next(app.audio);
            s_next_last = next_down;

            // arrow keys orbit camera
            if (glfwGetKey(app.window.handle, GLFW_KEY_LEFT)  == GLFW_PRESS) s_cam.yaw   -= Const::CAM_YAW_SPEED   * dt;
            if (glfwGetKey(app.window.handle, GLFW_KEY_RIGHT) == GLFW_PRESS) s_cam.yaw   += Const::CAM_YAW_SPEED   * dt;
            if (glfwGetKey(app.window.handle, GLFW_KEY_UP)    == GLFW_PRESS) s_cam.pitch += Const::CAM_PITCH_SPEED * dt;
            if (glfwGetKey(app.window.handle, GLFW_KEY_DOWN)  == GLFW_PRESS) s_cam.pitch -= Const::CAM_PITCH_SPEED * dt;
            s_cam.pitch = glm::clamp(s_cam.pitch, Const::CAM_PITCH_MIN, Const::CAM_PITCH_MAX);

            bool arrow_held = glfwGetKey(app.window.handle, GLFW_KEY_LEFT)  == GLFW_PRESS
                        || glfwGetKey(app.window.handle, GLFW_KEY_RIGHT) == GLFW_PRESS
                        || glfwGetKey(app.window.handle, GLFW_KEY_UP)    == GLFW_PRESS
                        || glfwGetKey(app.window.handle, GLFW_KEY_DOWN)  == GLFW_PRESS;

            // exp decay spring-back to behind trike while driving
            if (app.player.mode == PLAYER_DRIVING && std::abs(app.trike.speed) > 0.3f && !arrow_held)
                s_cam.yaw = glm::mix(s_cam.yaw, 0.0f, 1.0f - std::exp(-3.5f * dt));

            /******************************************************************************
             FIXED-TIMESTEP PHYSICS (120 Hz)
            real time accumulated and consumed in fixed chunks for stable sim
            ******************************************************************************/
            app.accumulator += dt;
            while (app.accumulator >= Const::FIXED_TIMESTEP){
                trike_physics_update(app.trike, input, app.map.terrain, Const::FIXED_TIMESTEP);
                app.accumulator -= Const::FIXED_TIMESTEP;
            }
            trike_model_update(app.scene.trike_model, app.trike.speed, dt);

            if (!app.trike.is_tipping && !app.trike.is_rolled_over)
                aabb_update(app.trike.aabb, app.trike.position, app.trike.heading);

            if (app.trike.impact_timer > 0.0f) app.trike.impact_timer -= dt;

            float pre_collision_speed = app.trike.speed;
            bool any_collision = false;
            collision_static_update(app, dt, pre_collision_speed, any_collision);

            // clamp speed to pre-collision value after all responses
            // without this, throttle held during wall contact gives free acceleration
            if (any_collision){
                float max_spd = std::abs(pre_collision_speed);
                if (std::abs(app.trike.speed) > max_spd + 0.5f)
                    app.trike.speed = std::copysign(max_spd, app.trike.speed);
                app.trike.lateral_speed = 0.0f;
            }

            collision_dynamic_update(app, dt);
            collision_dynamic_vs_dynamic(app);

            npc_update(app, dt, s_q_pickup);

            /******************************************************************************
             CAMERA
            ******************************************************************************/
            proj = glm::perspective(
                glm::radians(Const::CAM_FOV),
                (float)Const::WINDOW_WIDTH / (float)Const::WINDOW_HEIGHT,
                Const::CAM_NEAR, Const::CAM_FAR);
            view = cam_update(s_cam, app, dt, arrow_held);

            /******************************************************************************
             AUDIO UPDATE
            ******************************************************************************/
            {
                glm::vec3 lis_pos = (app.player.mode == PLAYER_FOOT)
                    ? app.player.pos : app.trike.position;
                glm::vec3 lis_fwd = glm::vec3(std::cos(app.trike.heading), 0.0f, std::sin(app.trike.heading));
                bool driving = (app.player.mode == PLAYER_DRIVING || app.player.mode == PLAYER_MOUNTING);
                audio_update(app.audio, dt, lis_pos, lis_fwd,
                    app.trike.speed, Const::TRIKE_MAX_SPEED, driving);
                audio_update_env(app.audio, dt, lis_pos,
                    app.map.ambience_zones, app.map.ambience_count, app.scene.night_factor);
            }

        } // end !settings_open input+physics+audio gate

        /******************************************************************************
         RENDER
        ******************************************************************************/
        glClearColor(Const::CLEAR_R, Const::CLEAR_G, Const::CLEAR_B, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // SHADOW PASS
        scene_update_daytime(app.scene, dt);
        app.editor_renderer.sun_dir        = app.scene.sun_dir;
        app.editor_renderer.light_color    = app.scene.light_color;
        app.editor_renderer.ambient        = app.scene.ambient;
        app.editor_renderer.diff_intensity = app.scene.diff_intensity;
        app.editor_renderer.shadow_cull_center = app.trike.position;
        scene_shadow_pass(app.scene, app.obstacles, app.trike.position);

        
        if (my_settings.render_shadows){
            glBindFramebuffer(GL_FRAMEBUFFER, app.scene.shadow_fbo);
            glViewport(0, 0, my_settings.shadow_map_size, my_settings.shadow_map_size);
            glClear(GL_DEPTH_BUFFER_BIT);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            editor_renderer_shadow_pass(app.editor_renderer, app.map,
                app.scene.light_space_mat, app.dynamic_sims);
            scene_trike_shadow_draw(app.scene, app.trike);
            driver_model_draw(app.scene.driver_model, app.player, app.trike,
                app.scene.shadow_shader, app.scene.light_space_mat, glm::mat4(1.0f),
                app.editor.pose_quat, app.editor.pose_offset, app.editor.pose_seat);

            for (const auto& npc : app.npcs){
                glm::vec3 dnpc = npc.position - app.trike.position;
                dnpc.y = 0.0f;
                float npc_cull_sq = my_settings.npc_cull_dist * my_settings.npc_cull_dist;
                if (glm::dot(dnpc, dnpc) > npc_cull_sq) continue;
                auto it = app.npc_model_cache.find(npc.model_path);
                DriverModel* mdl = (it != app.npc_model_cache.end())
                    ? &it->second : &app.scene.driver_model;
                npc_draw(npc, *mdl, app.scene.shadow_shader,
                    app.scene.light_space_mat, glm::mat4(1.0f));
            }
            glCullFace(GL_BACK);
            glDisable(GL_CULL_FACE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        else {
            // wipe the depth map to white (1.0) so the lit shader sees no shadow
            // without this, the stale FBO content projects ghost shadows when toggled off
            glBindFramebuffer(GL_FRAMEBUFFER, app.scene.shadow_fbo);
            glViewport(0, 0, my_settings.shadow_map_size, my_settings.shadow_map_size);
            glClearDepth(1.0);
            glClear(GL_DEPTH_BUFFER_BIT);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        {
            int fb_w, fb_h;
            glfwGetFramebufferSize(app.window.handle, &fb_w, &fb_h);
            glViewport(0, 0, fb_w, fb_h);
        }

        app.editor_renderer.shadow_depth_tex = app.scene.shadow_depth_tex;
        app.editor_renderer.light_space_mat  = app.scene.light_space_mat;
        app.editor_renderer.night_factor = app.scene.night_factor;
        app.editor_renderer.fog_color = app.scene.fog_color;
        app.editor_renderer.fog_near = app.scene.fog_near;
        app.editor_renderer.fog_far = app.scene.fog_far;

        std::vector<LightSource> frame_lights = app.map.lights;
        if (app.player.headlights_on && app.player.mode == PLAYER_DRIVING)
            frame_lights.push_back(trike_headlight(app.trike.position, app.trike.heading));

        scene_draw_sky(app.scene, view, proj);
        scene_draw(app.scene, app.trike, app.obstacles, frame_lights, view, proj, app.editor.show_hitboxes);

        // build flash map from current hit timers
        std::map<int, float> flash_map;
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
        editor_renderer_draw_terrain_surface(app.editor_renderer, app.map.terrain, view, proj,
            app.map.ocean);
        editor_renderer_draw_props(app.editor_renderer, app.map, view, proj,
            flash_map, app.dynamic_sims, frame_lights, true);
        scene_draw_driver(app.scene, app.player, app.trike, view, proj,
            app.editor_renderer.obj_shader,
            app.editor.pose_quat, app.editor.pose_offset, app.editor.pose_seat);

        for (const auto& npc : app.npcs){
            glm::vec3 d = npc.position - app.trike.position;
            d.y = 0.0f;
            float npc_cull_sq = my_settings.npc_cull_dist * my_settings.npc_cull_dist;
            if (glm::dot(d, d) > npc_cull_sq) continue;
            auto it = app.npc_model_cache.find(npc.model_path);
            DriverModel* mdl = (it != app.npc_model_cache.end())
                ? &it->second : &app.scene.driver_model;
            npc_draw(npc, *mdl, app.editor_renderer.obj_shader, view, proj);
        }

        // DESTINATION MARKER - glowing ring + HUD arrow while passenger is riding
        if (app.passenger_npc_id != -1){
            for (const auto& npc : app.npcs){
                if (npc.id != app.passenger_npc_id) continue;

                glm::vec3 drop = npc.drop_point;
                drop.y = heightfield_sample(app.map.terrain, drop.x, drop.z);
                float pulse = 0.85f + 0.15f * std::sin((float)glfwGetTime() * 3.0f);
                scene_draw_drop_marker(app.scene, drop, pulse, view, proj);

                glm::vec3 to_drop = drop - app.trike.position;
                to_drop.y = 0.0f;
                float dist_to_drop = glm::length(to_drop);
                if (dist_to_drop > 5.0f){
                    glm::vec3 trike_fwd = { std::cos(app.trike.heading), 0.0f, std::sin(app.trike.heading) };
                    glm::vec3 dir_to_drop = to_drop / dist_to_drop;
                    float dot    = glm::dot(trike_fwd, dir_to_drop);
                    float cross_y = trike_fwd.x * dir_to_drop.z - trike_fwd.z * dir_to_drop.x;
                    hud_draw_direction_arrow(app.hud, dot, cross_y, dist_to_drop);
                }
                break;
            }
        }

        // RAIN
        glm::vec3 rain_origin = (app.player.mode == PLAYER_FOOT) ? app.player.pos : app.trike.position;
        float rain_speed = (app.player.mode == PLAYER_FOOT) ? app.player.speed : app.trike.speed;
        if (!app.editor.settings_open){
            app.scene.sky_rain_target = app.rain.active ? 1.0f : 0.0f;
            rain_tick_trigger(app.rain, dt);
            audio_rain_set(app.audio, app.rain.active);
            rain_tick_thunder(app.rain, dt, "../assets");
            if (app.rain.thunder_boom_pending && app.rain.thunder_audio_delay <= 0.0f){
                app.rain.thunder_boom_pending = false;
                audio_trigger_voice_local(app.audio, "../assets/audio/ambience/thunder.wav");
            }
            rain_update(app.rain, dt, rain_origin, rain_speed, app.trike.heading, app.map.terrain);
        }
        rain_draw(app.rain, view, proj, rain_origin, rain_speed, app.trike.heading);

        std::string radio_track = (app.audio.radio_on && app.audio.radio_index < (int)app.audio.radio_playlist.size())
            ? app.audio.radio_playlist[app.audio.radio_index] : "";
        bool driving = (app.player.mode == PLAYER_DRIVING || app.player.mode == PLAYER_MOUNTING);
        if (my_settings.show_hud && driving) hud_draw(app.hud, app.trike, app.passenger_npc_id != -1, app.passenger_fare, app.audio.radio_on, radio_track);
        if (app.editor.settings_open){
            editor_input_settings(app.editor, app.window.handle);
            editor_renderer_draw_settings_menu(app.editor_renderer, app.editor);
        }
        window_swap_buffers(app.window);
        window_poll_events();
    }
}

/*****************************************************
 APP_SHUTDOWN
*****************************************************/
void app_shutdown(App& app){
    rain_destroy(app.rain);
    settings_save();
    audio_shutdown(app.audio);
    hud_destroy(app.hud);
    scene_destroy(app.scene);
    window_destroy(app.window);
    world_map_save(app.map, Const::MAP_SAVE_PATH);
    ambience_save(app.map.ambience_zones, app.map.ambience_count, Const::AMBIENCE_SAVE_PATH);
    editor_renderer_destroy(app.editor_renderer);
    app.running = false;
}