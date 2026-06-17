#pragma once
#include "app.hpp"

// load NPC models and spawn NpcState for every PEDESTRIAN world object
// called on init and on every editor->drive switch when map is dirty
void init_npcs(App& app);

// per-frame NPC logic: passenger lock, hailing, pickup, voice, ragdoll, dropoff
void npc_update(App& app, float dt, bool s_q_pickup);