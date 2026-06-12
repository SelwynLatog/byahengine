#include "ambience_zone.hpp"
#include <cstdio>
#include <cstring>
#include <iostream>

bool ambience_save(const AmbienceZone* zones, int count, const char* path){
    FILE* f = fopen(path, "w");
    if (!f){
        std::cerr << "[ambience] failed to open for write: " << path << "\n";
        return false;
    }
    for (int i = 0; i < count; i++){
        const AmbienceZone& z = zones[i];
        fprintf(f, "ZONE %d %.4f %.4f %.4f %.4f %d %s %d\n",
            z.id,
            z.pos.x, z.pos.y, z.pos.z,
            z.radius,
            (int)z.type,
            z.audio_path,
            z.night_only ? 1 : 0);
    }
    fclose(f);
    std::cout << "[ambience] saved " << count << " zones to " << path << "\n";
    return true;
}

bool ambience_load(AmbienceZone* zones, int& count, int max_count, const char* path){
    count = 0;
    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[512];
    while (fgets(line, sizeof(line), f)){
        if (strncmp(line, "ZONE", 4) != 0) continue;
        if (count >= max_count) break;

        AmbienceZone& z = zones[count];
        int type_int = 0, night_int = 0;
        int parsed = sscanf(line, "ZONE %d %f %f %f %f %d %255s %d",
            &z.id,
            &z.pos.x, &z.pos.y, &z.pos.z,
            &z.radius,
            &type_int,
            z.audio_path,
            &night_int);

        if (parsed < 7) continue; // malformed line, skip
        z.type = (AmbienceType)type_int;
        z.night_only = night_int != 0;
        count++;
    }
    fclose(f);
    std::cout << "[ambience] loaded " << count << " zones from " << path << "\n";
    return true;
}