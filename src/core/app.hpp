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
#include "../world/npc.hpp"
#include "../audio/audio.hpp"


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

    // NPC system
    std::vector<NpcState> npcs;
    int passenger_npc_id = -1; // -1 = no passenger
    float passenger_fare = 0.0f; // accumulated fare for current ride

    // one DriverModel per unique entity model_path
    // keyed by filename eg. "female_1.obj"
    // loaded once on init_npcs, reused across all npcs sharing the same model
    std::unordered_map<std::string, DriverModel> npc_model_cache;

    AudioSystem audio;
    float passenger_time = 0.0f; // accumulates while npc is passenger, drives slow dropoff
};

void app_init(App& app);
void app_run(App& app);
void app_shutdown(App& app);

// rebuild app.obstacles from all Static WorldMap objects
void world_map_to_obstacles(App& app);