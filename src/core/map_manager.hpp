#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

// runtime map paths replaces Const::MAP_SAVE_PATH / AMBIENCE_SAVE_PATH
// switch active_dir to redirect all saves/loads to a different map folder

struct MapEntry {
    std::string dir; // eg. "../assets/maps/poblacion"
    std::string name; // display name from meta.txt
};

struct MapManager {
    std::vector<MapEntry> maps;
    int active_index = 0;   // index into maps[]
    int loaded_index = 0;   // index active [] map in mem
    std::string loaded_dir; // dir of the loaded map
    bool rename_mode = false;
    std::string rename_buf;
    std::string rename_target_dir;

    std::string map_path() const { return maps[active_index].dir + "/map.txt"; }
    std::string ambience_path() const { return maps[active_index].dir + "/_ambience.amb"; }
    std::string lights_path() const { return maps[active_index].dir + "/map.lt"; }
};

inline MapManager g_maps;

inline std::string map_read_name(const std::string& dir){
    std::ifstream f(dir + "/meta.txt");
    std::string name;
    if (f.is_open()) std::getline(f, name);
    return name.empty() ? "Unnamed" : name;
}

inline void map_write_name(const std::string& dir, const std::string& name){
    std::ofstream f(dir + "/meta.txt");
    if (f.is_open()) f << name;
}

inline void map_manager_scan(){
    // remember which dir was active BEFORE rebuilding the vector
    // active_index is a raw index into maps[] - clearing/re-sorting the vector
    // silently invalidates it unless we re-resolve by identity (dir) afterward
    std::string prev_active_dir;
    if (g_maps.active_index >= 0 && g_maps.active_index < (int)g_maps.maps.size())
        prev_active_dir = g_maps.maps[g_maps.active_index].dir;

    g_maps.maps.clear();
    std::string base = "../assets/maps";
    if (!std::filesystem::exists(base))
        std::filesystem::create_directories(base);
    for (const auto& entry : std::filesystem::directory_iterator(base)){
        if (!entry.is_directory()) continue;
        MapEntry e;
        e.dir  = entry.path().string();
        std::replace(e.dir.begin(), e.dir.end(), '\\', '/');
        e.name = map_read_name(e.dir);
        g_maps.maps.push_back(e);
    }
    std::sort(g_maps.maps.begin(), g_maps.maps.end(),
        [](const MapEntry& a, const MapEntry& b){ return a.dir < b.dir; });

    // re-resolve active_index to point at the same dir as before the rescan
    // falls back to 0 if that dir no longer exists (e.g. it was deleted)
    if (!prev_active_dir.empty()){
        bool found = false;
        for (int i = 0; i < (int)g_maps.maps.size(); i++){
            if (g_maps.maps[i].dir == prev_active_dir){
                g_maps.active_index = i;
                found = true;
                break;
            }
        }
        if (!found) g_maps.active_index = 0;
    }

    std::cout << "[maps] found " << g_maps.maps.size() << " maps\n";
}

// creates a new map folder with a blank stub then switches to it
inline int map_manager_new(const std::string& name = "New Map"){
    std::string base = "../assets/maps";
    int idx = (int)g_maps.maps.size();
    std::string dir = base + "/map" + std::to_string(idx);
    while (std::filesystem::exists(dir))
        dir = base + "/map" + std::to_string(++idx);
    std::filesystem::create_directories(dir);
    map_write_name(dir, name);
    // write empty map.txt so world_map_load succeeds
    { std::ofstream f(dir + "/map.txt"); }
    map_manager_scan();
    for (int i = 0; i < (int)g_maps.maps.size(); i++)
        if (g_maps.maps[i].dir == dir){ g_maps.active_index = i; return i; }
    return 0;
}