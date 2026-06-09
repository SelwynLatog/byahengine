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

    // save audio assignments to sibling file
    std::string audio_path = (base.parent_path() / (base.stem().string() + "_audio.au")).string();
    {
        std::ofstream af(audio_path);
        for (const auto& o : map.objects){
            // only write objects that have at least one audio field set
            bool has_audio = !o.audio_impact.empty()
                || !o.audio_proximity.empty()
                || !o.audio_hail.empty()
                || !o.audio_pickup.empty()
                || !o.audio_yap.empty()
                || !o.audio_dropoff_good.empty()
                || !o.audio_dropoff_bad.empty()
                || !o.audio_crash_mild.empty()
                || !o.audio_crash_heavy.empty()
                || !o.audio_crash_rollover.empty();
            if (!has_audio) continue;
            af << o.id << " "
               << "\"" << o.audio_impact << "\" "
               << "\"" << o.audio_proximity << "\" "
               << o.audio_radius << " "
               << "\"" << o.audio_hail << "\" "
               << "\"" << o.audio_pickup << "\" "
               << "\"" << o.audio_yap << "\" "
               << "\"" << o.audio_dropoff_good << "\" "
               << "\"" << o.audio_dropoff_bad << "\" "
               << "\"" << o.audio_crash_mild << "\" "
               << "\"" << o.audio_crash_heavy << "\" "
               << "\"" << o.audio_crash_rollover << "\"\n";
        }
    }

    // save per-NPC poses to sibling file
    std::string poses_path = (base.parent_path() / (base.stem().string() + "_npc_poses.np")).string();
    {
        std::ofstream pf(poses_path);
        for (const auto& o : map.objects){
            if (o.behavior != PEDESTRIAN) continue;
            pf << o.id;
            for (int i = 0; i < 6; i++)
                pf << " " << o.npc_hail_quat[i].w << " " << o.npc_hail_quat[i].x
                   << " " << o.npc_hail_quat[i].y << " " << o.npc_hail_quat[i].z;
            for (int i = 0; i < 6; i++)
                pf << " " << o.npc_hail_offset[i].x << " " << o.npc_hail_offset[i].y << " " << o.npc_hail_offset[i].z;
            pf << " " << o.npc_hail_seat.x << " " << o.npc_hail_seat.y << " " << o.npc_hail_seat.z;
            for (int i = 0; i < 6; i++)
                pf << " " << o.npc_mount_quat[i].w << " " << o.npc_mount_quat[i].x
                   << " " << o.npc_mount_quat[i].y << " " << o.npc_mount_quat[i].z;
            for (int i = 0; i < 6; i++)
                pf << " " << o.npc_mount_offset[i].x << " " << o.npc_mount_offset[i].y << " " << o.npc_mount_offset[i].z;
            pf << " " << o.npc_mount_seat.x << " " << o.npc_mount_seat.y << " " << o.npc_mount_seat.z << "\n";
        }
    }
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

    // load per-NPC poses, optional 
    // missing file means all poses stay default
    std::string poses_path = (base.parent_path() / (base.stem().string() + "_npc_poses.np")).string();
    if (std::filesystem::exists(poses_path)){
        std::ifstream pf(poses_path);
        std::string pline;
        while (std::getline(pf, pline)){
            if (pline.empty()) continue;
            std::istringstream ps(pline);
            int id; ps >> id;
            for (auto& o : map.objects){
                if (o.id != id) continue;
                for (int i = 0; i < 6; i++)
                    ps >> o.npc_hail_quat[i].w >> o.npc_hail_quat[i].x
                       >> o.npc_hail_quat[i].y >> o.npc_hail_quat[i].z;
                for (int i = 0; i < 6; i++)
                    ps >> o.npc_hail_offset[i].x >> o.npc_hail_offset[i].y >> o.npc_hail_offset[i].z;
                ps >> o.npc_hail_seat.x >> o.npc_hail_seat.y >> o.npc_hail_seat.z;
                for (int i = 0; i < 6; i++)
                    ps >> o.npc_mount_quat[i].w >> o.npc_mount_quat[i].x
                       >> o.npc_mount_quat[i].y >> o.npc_mount_quat[i].z;
                for (int i = 0; i < 6; i++)
                    ps >> o.npc_mount_offset[i].x >> o.npc_mount_offset[i].y >> o.npc_mount_offset[i].z;
                ps >> o.npc_mount_seat.x >> o.npc_mount_seat.y >> o.npc_mount_seat.z;
                break;
            }
        }
        std::cout << "world_map loaded npc poses from " << poses_path << "\n";
    }

    // load audio assignments
    std::string audio_path = (base.parent_path() / (base.stem().string() + "_audio.au")).string();
    if (std::filesystem::exists(audio_path)){
        std::ifstream af(audio_path);
        std::string aline;
        while (std::getline(af, aline)){
            if (aline.empty()) continue;
            std::istringstream as(aline);
            int id; as >> id;
            for (auto& o : map.objects){
                if (o.id != id) continue;
                // quoted strings handle empty fields cleanly
                auto read_quoted = [&](std::string& out){
                    char c; std::string tmp;
                    while (as.get(c) && c != '"');
                    while (as.get(c) && c != '"') tmp += c;
                    out = tmp;
                };
                read_quoted(o.audio_impact);
                read_quoted(o.audio_proximity);
                as >> o.audio_radius;
                read_quoted(o.audio_hail);
                read_quoted(o.audio_pickup);
                read_quoted(o.audio_yap);
                read_quoted(o.audio_dropoff_good);
                read_quoted(o.audio_dropoff_bad);
                read_quoted(o.audio_crash_mild);
                read_quoted(o.audio_crash_heavy);
                read_quoted(o.audio_crash_rollover);
                break;
            }
        }
        std::cout << "world_map loaded audio from " << audio_path << "\n";
    }

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