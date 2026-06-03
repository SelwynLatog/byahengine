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

void world_map_light_place(WorldMap& map, LightSource light){
    light.id = map.next_light_id++;
    map.lights.push_back(light);
}

bool world_map_light_remove(WorldMap& map, int id){
    for (auto it = map.lights.begin(); it != map.lights.end(); it++){
        if (it->id == id){
            map.lights.erase(it);
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
          << o.mass << " " << o.restitution << " " << o.friction << " "
          << o.npc_type << " " << (o.npc_can_hail ? 1 : 0) << " "
          << o.npc_walk_a.x << " " << o.npc_walk_a.y << " " << o.npc_walk_a.z << " "
          << o.npc_walk_b.x << " " << o.npc_walk_b.y << " " << o.npc_walk_b.z << " "
          << o.npc_drop_point.x << " " << o.npc_drop_point.y << " " << o.npc_drop_point.z << " "
          << o.npc_weight << "\n";
    }
    
    std::cout << "world_map saved " << map.objects.size() << " objects to " << path << "\n";

    // derive sibling paths from the map path
    std::filesystem::path base(path);
    std::string terrain_path = (base.parent_path() / (base.stem().string() + "_terrain.hf")).string();
    std::string lights_path = (base.parent_path() / (base.stem().string() + "_lights.lt")).string();
    {
        std::ofstream lf(lights_path);
        for (const auto& l : map.lights){
            lf << l.id << " "
               << l.position.x << " " << l.position.y << " " << l.position.z << " "
               << l.color.r << " " << l.color.g << " " << l.color.b << " "
               << l.radius << " " << l.intensity << "\n";
        }
    }

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
        // npc fields 
        // optional, older maps leave defaults intact
        int can_hail_int = 0;
        ss >> o.npc_type >> can_hail_int
           >> o.npc_walk_a.x >> o.npc_walk_a.y >> o.npc_walk_a.z
           >> o.npc_walk_b.x >> o.npc_walk_b.y >> o.npc_walk_b.z
           >> o.npc_drop_point.x >> o.npc_drop_point.y >> o.npc_drop_point.z
           >> o.npc_weight;
        o.npc_can_hail = (can_hail_int == 1);

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

    map.lights.clear();
    map.next_light_id = 0;
    std::string lights_path = (base.parent_path() / (base.stem().string() + "_lights.lt")).string();
    if (std::filesystem::exists(lights_path)){
        std::ifstream lf(lights_path);
        std::string lline;
        while (std::getline(lf, lline)){
            if (lline.empty()) continue;
            std::istringstream ss(lline);
            LightSource l;
            ss >> l.id
               >> l.position.x >> l.position.y >> l.position.z
               >> l.color.r >> l.color.g >> l.color.b
               >> l.radius >> l.intensity;
            if (l.id >= map.next_light_id) map.next_light_id = l.id + 1;
            map.lights.push_back(l);
        }
        std::cout << "world_map loaded " << map.lights.size() << " lights\n";
    }
    return true;
}