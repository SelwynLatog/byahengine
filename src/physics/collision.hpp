#pragma once
#include "../core/app.hpp"

// rebuild app.obstacles from all STATIC world objects
// called on init and on every editor->drive switch when map is dirty
void world_map_to_obstacles(App& app);

// run static obstacle collision + response against trike
// suspended during tumble/rollover
// returns true if any collision occurred this frame
bool collision_static_update(App& app, float dt, float& pre_collision_speed, bool& any_collision);

// integrate all dynamic objects: gravity, drag, angular, sleep
// includes trike vs dynamic and dynamic vs static collision
void collision_dynamic_update(App& app, float dt);

// dynamic vs dynamic collision 
// O(n^2), small baranggay will change at some point
void collision_dynamic_vs_dynamic(App& app);