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
#include <cmath>

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

void app_init(App& app){
    window_init(app.window,
        Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT, Const::WINDOW_TITLE);

    glEnable(GL_DEPTH_TEST);

    scene_init(app.scene);
    editor_renderer_init(app.editor_renderer);

    // scan assets/ for placeable props
    editor_scan_props(app.editor, "../assets");

    // try to load existing map
    world_map_load(app.map, Const::MAP_SAVE_PATH);   

    // spawn static obstacles
    // position=center-bottom 
    // half_extents=half w/h/d

    app.obstacles.push_back(make_obstacle({10.0f, 0.0f,  0.0f}, {0.75f, 1.0f, 0.75f}));
    app.obstacles.push_back(make_obstacle({ 0.0f, 0.0f, 10.0f}, {1.0f,  1.5f, 0.5f}));
    app.obstacles.push_back(make_obstacle({15.0f, 0.0f,  8.0f}, {0.5f,  0.8f, 0.5f}));

    hud_init(app.hud, Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT);

    app.last_time   = (float)glfwGetTime();
    app.accumulator = 0.0f;
    app.running     = true;
}

// main loop app_run

void app_run(App& app){
    while (!window_should_close(app.window)){
        // delta time
        float now = (float)glfwGetTime();
        float dt  = now - app.last_time;
        app.last_time = now;
        if (dt > Const::MAX_DELTA) dt = Const::MAX_DELTA;

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
        }
        s_tab_pressed_last = tab_down;

        // EDITOR MODE
        if (app.editor.active){
            editor_cam_update(app.editor, app.window.handle, dt);

            glm::mat4 view = editor_cam_get_view(app.editor);
            glm::mat4 proj = glm::perspective ( glm::radians(Const::CAM_FOV), (float)Const::WINDOW_WIDTH/ (float)Const::WINDOW_HEIGHT,
            Const::CAM_NEAR, Const::CAM_FAR);

            editor_input_update(app.editor, app.map, app.editor_renderer, app.window.handle, view, proj, Const::WINDOW_WIDTH, Const::WINDOW_HEIGHT, dt);

            // render
            glClearColor(Const::CLEAR_R, Const::CLEAR_G, Const::CLEAR_B, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // draw world scene:
            // ground, gizmo
            // trike parked where it stopped last
            scene_draw(app.scene, app.trike, app.obstacles, view, proj);

            // draw editor overlays:
            // grid, ghost, selection highlight
            editor_renderer_draw(app.editor_renderer, app.editor, app.map, view, proj);

            // temp editor control hud
            font_draw(app.editor_renderer.font,"[TAB] drive  [L CLICK] place/select  [DEL] delete  [T]translate [R]rotate [Y]scale  [Ctrl+S] save [PgUp/PgDn] Y nudge",
                      10, Const::WINDOW_HEIGHT - 30, 2, 0.7f, 0.7f, 0.7f);
            
            window_swap_buffers(app.window);
            window_poll_events();
            continue;
        }


        // DRIVING MODE
        TrikeInput input;
        input.throttle = (glfwGetKey(app.window.handle, GLFW_KEY_W) == GLFW_PRESS) ? 1.0f : 0.0f;
        input.brake = (glfwGetKey(app.window.handle, GLFW_KEY_S) == GLFW_PRESS) ? 1.0f : 0.0f;
        input.steer = 0.0f;
        if (glfwGetKey(app.window.handle, GLFW_KEY_A) == GLFW_PRESS) input.steer -= 1.0f;
        if (glfwGetKey(app.window.handle, GLFW_KEY_D) == GLFW_PRESS) input.steer += 1.0f;
        bool f_down = glfwGetKey(app.window.handle, GLFW_KEY_F) == GLFW_PRESS;
        if (f_down && !s_f_pressed_last) s_free_cam = !s_free_cam;
        s_f_pressed_last = f_down;

        // camera orbit input
        if (glfwGetKey(app.window.handle, GLFW_KEY_LEFT)  == GLFW_PRESS) s_cam_yaw -= Const::CAM_YAW_SPEED   * dt;
        if (glfwGetKey(app.window.handle, GLFW_KEY_RIGHT) == GLFW_PRESS) s_cam_yaw += Const::CAM_YAW_SPEED   * dt;
        if (glfwGetKey(app.window.handle, GLFW_KEY_UP)    == GLFW_PRESS) s_cam_pitch += Const::CAM_PITCH_SPEED * dt;
        if (glfwGetKey(app.window.handle, GLFW_KEY_DOWN)  == GLFW_PRESS) s_cam_pitch -= Const::CAM_PITCH_SPEED * dt;
        s_cam_pitch = glm::clamp(s_cam_pitch, Const::CAM_PITCH_MIN, Const::CAM_PITCH_MAX);

        // fixed timestep physics
        // physics will run constantly at 120 hz regardless if framerate is ass
        // accumulate real tim & consume it in fixed chunks
        // this prevents the sim from going insane in slow frames
        app.accumulator += dt;
        while (app.accumulator >= Const::FIXED_TIMESTEP){
            trike_physics_update(app.trike, input, Const::FIXED_TIMESTEP);
            app.accumulator -= Const::FIXED_TIMESTEP;
        }

        // update trike AABB after physics
        aabb_update(app.trike.aabb, app.trike.position, app.trike.heading);

        // tick impact timer
        if (app.trike.impact_timer > 0.0f) app.trike.impact_timer -= dt;

        // collision detection + response
        for (auto& obs : app.obstacles){
            if (obs.hit_timer > 0.0f) obs.hit_timer -= dt;
            glm::vec3 to_obs = obs.position - app.trike.position;
            if (glm::dot(to_obs, to_obs) > 25.0f) continue;
            if (!aabb_overlap(app.trike.aabb, obs.aabb)) continue;

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

            if (closing > 0.8f){
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
            } else {
                if (spd_along < 0.0f) app.trike.speed -= spd_along;
                if (lat_along < 0.0f) app.trike.lateral_speed -= lat_along;
            }

            aabb_update(app.trike.aabb, app.trike.position, app.trike.heading);
        }

        // camera
        float yaw_r = glm::radians(s_cam_yaw);
        float pitch_r = glm::radians(s_cam_pitch);
        float cam_yaw_world = app.trike.heading + yaw_r + glm::radians(180.0f);

        glm::vec3 cam_origin = app.trike.position + glm::vec3(0.0f, Const::CAM_ORBIT_TARGET_Y, 0.0f);
        glm::vec3 ideal_eye  = cam_origin + glm::vec3(
            s_cam_dist * cosf(pitch_r) * cosf(cam_yaw_world),
            s_cam_dist * sinf(pitch_r),
            s_cam_dist * cosf(pitch_r) * sinf(cam_yaw_world));
        s_cam_pos = glm::mix(s_cam_pos, ideal_eye, Const::CAM_LERP_SPEED * dt);

        float fwd_angle = app.trike.heading;
        glm::vec3 fwd = glm::vec3(cosf(fwd_angle), 0.0f, sinf(fwd_angle));
        float lookahead = (app.trike.speed / Const::TRIKE_MAX_SPEED) * Const::CAM_LOOKAHEAD;
        glm::vec3 target = cam_origin + fwd * lookahead;

        // camera shake on impact
        // shake the lookat target and not the eye position
        // this feels more natural and looks more natural
        // 3 sin.cos at diff frequencies gives pseudorandom wobble without actual RNG
        // decays at zero as impact_timer runs out
        // pretty sweet if i must say
        if (app.trike.impact_timer > 0.0f){
            float t = app.trike.impact_timer;
            float mag = glm::clamp(app.trike.last_impact_force * 0.04f, 0.0f, 0.4f);
            float decay = t / 0.35f;
            target.x += std::sin(t * 47.0f) * mag * decay;
            target.y += std::cos(t * 31.0f) * mag * decay;
            target.z += std::sin(t * 23.0f) * mag * decay;
        }

        glm::mat4 view;
        if (s_free_cam){
            glm::vec3 top = app.trike.position + glm::vec3(0.0f, 15.0f, 0.0f);
            view = glm::lookAt(top, app.trike.position, glm::vec3(1,0,0));
        } else {
            view = glm::lookAt(s_cam_pos, target, glm::vec3(0,1,0));
        }

        glm::mat4 proj = glm::perspective(
            glm::radians(Const::CAM_FOV),
            (float)Const::WINDOW_WIDTH / (float)Const::WINDOW_HEIGHT,
            Const::CAM_NEAR, Const::CAM_FAR);

        // render
        glClearColor(Const::CLEAR_R, Const::CLEAR_G, Const::CLEAR_B, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // entire render pass in one call
        // ground, gizmo, trike, obstacles, wireframes
        scene_draw(app.scene, app.trike, app.obstacles, view, proj);
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