#include "world_map.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

WorldObject& world_map_place(WorldMap& map, WorldObject obj){
    obj.id = map.next_id++;
    map.objects.push_back(obj);
    return map.objects.back();
}

bool world_map_remove(WorldMap& map, int id){
    for(auto it = map.objects.begin(); it!= map.objects.end(); it++){
        if(it ->id == id){
            map.objects.erase(it);
            return true;
        }
    }
    return false;
}

void world_map_save(const WorldMap& map, const std::string& path){
    std::ofstream f(path);
    if (!f){
        std::cerr << "world_map failed to open for save: " << path << "\n";
        return;
    }

    for (const auto& o : map.objects){
        f << o.id << " "
          << o.behavior << " "
          << o.position.x << " " << o.position.y << " " << o.position.z << " "
          << o.rotation.x << " " << o.rotation.y << " " << o.rotation.z << " "
          << o.scale.x << " " << o.scale.y << " " << o.scale.z << " "
          << o.model_path << " " << o.y_floor_offset << " "
          << o.mass << " " << o.restitution << " " << o.friction << "\n";
    }
    std::cout << "world_map saved " << map.objects.size() << " objects to " << path << "\n";

    // derive sibling paths from the map path
    // objects stay in the main file, terrain and roads get their own files
    // keeps map path small and safe
    // by safe I mean in case I fuck up I dont want to restart the entire map
    std::filesystem::path base(path);
    std::string terrain_path = (base.parent_path() / (base.stem().string() + "_terrain.hf")).string();
    std::string roads_path = (base.parent_path() / (base.stem().string() + "_roads.rd")).string();

    heightfield_save(map.terrain, terrain_path);
    road_splines_save(map.roads, roads_path);
    std::string ocean_path = (base.parent_path() / (base.stem().string() + "_ocean.oc")).string();
    ocean_save(map.ocean, ocean_path);
}

bool world_map_load(WorldMap& map, const std::string& path){
    std::ifstream f(path);
    if (!f){
        std::cerr << "world_map failed to open for load: " << path << "\n";
        return false;
    }

    map.objects.clear();
    map.next_id = 0;

    std::string line;
    while (std::getline(f, line)){
        if (line.empty()) continue;
        std::istringstream ss(line);

        WorldObject o;
        int behavior_int;
        ss >> o.id >> behavior_int
           >> o.position.x >> o.position.y >> o.position.z
           >> o.rotation.x >> o.rotation.y >> o.rotation.z
           >> o.scale.x >> o.scale.y >> o.scale.z
           >> o.model_path >> o.y_floor_offset
           >> o.mass >> o.restitution >> o.friction;
        // older map files won't have mass/restitution/friction
        // ss will just leave defaults intact if fields are missing

        o.behavior = (ObjectBehavior)behavior_int;
        if (o.id >= map.next_id) map.next_id = o.id + 1;
        map.objects.push_back(o);
    }
    std::cout << "world_map loaded " << map.objects.size() << " objects from " << path << "\n";

    // load terrain and roads from sibling files if they exist
    // missing files are not an error
    // fresh map starts flat with no roads
    std::filesystem::path base(path);
    std::string terrain_path = (base.parent_path() / (base.stem().string() + "_terrain.hf")).string();
    std::string roads_path = (base.parent_path() / (base.stem().string() + "_roads.rd")).string();

    if (std::filesystem::exists(terrain_path))
        heightfield_load(map.terrain, terrain_path);
    if (std::filesystem::exists(roads_path))
        road_splines_load(map.roads, roads_path);
    std::string ocean_path = (base.parent_path() / (base.stem().string() + "_ocean.oc")).string();
    if (std::filesystem::exists(ocean_path))
        ocean_load(map.ocean, ocean_path);
    return true;
}