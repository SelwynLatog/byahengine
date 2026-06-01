#pragma once
#include "window.hpp"
#include "editor_state.hpp"
#include "../physics/trike_state.hpp"
#include "../physics/trike_physics.hpp"
#include "../physics/obstacle.hpp"
#include "../renderer/hud.hpp"
#include "../renderer/scene.hpp"
#include "../renderer/editor_renderer.hpp"
#include "../world/world_map.hpp"
#include "../world/world_object.hpp"
#include <vector>
#include <unordered_map>
#include "../physics/dynamic_sim.hpp"
#include "player_state.hpp"


struct App {
    Window window;
    TrikeState trike;
    PlayerState player;
    Hud hud;
    SceneState scene;
    float last_time = 0.0f;
    float accumulator = 0.0f;
    bool running = false;

    std::vector<Obstacle> obstacles;

    EditorState editor;
    WorldMap map;
    EditorRenderer editor_renderer;

    std::unordered_map<int, DynamicSim> dynamic_sims;
    std::unordered_map<int, const WorldObject*> wo_by_id;
    bool obstacles_dirty = true;
};

void app_init(App& app);
void app_run(App& app);
void app_shutdown(App& app);

// rebuild app.obstacles from all Static WorldMap objects
void world_map_to_obstacles(App& app);