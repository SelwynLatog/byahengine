#pragma once
#include "world_object.hpp"
#include <vector>
#include <string>

struct WorldMap{
    std::vector<WorldObject> objects;
    int next_id = 0;
};

// add object to map
// assigns unique id
// returns ref to the placed object
WorldObject& world_map_place(WorldMap& map, WorldObject obj);

// removes object by id
// returns true if found and removed
bool world_map_remove(WorldMap& map, int id);

// saves map to a simple text file
// format here is one object per line
// fields separated via space
void world_map_save(const WorldMap& map, const std::string& path);

// loads map from file
// replaces current map contents
bool world_map_load(WorldMap& map, const std::string& path);
