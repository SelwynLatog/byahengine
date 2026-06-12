#pragma once
#include <glm/glm.hpp>
#include "../core/const.hpp"

enum AmbienceType {
    AMBIENCE_PROXIMITY, // always-on looping sfx within radius
    AMBIENCE_NIGHT  // same but only active when night_factor > NIGHT_THRESH
};

struct AmbienceZone {
    int id = -1;
    glm::vec3 pos = glm::vec3(0.0f);
    float radius = 20.0f;
    AmbienceType type = AMBIENCE_PROXIMITY;
    char audio_path[256] = {};
    bool night_only = false;
};

bool ambience_save(const AmbienceZone* zones, int count, const char* path);
bool ambience_load(AmbienceZone* zones, int& count, int max_count, const char* path);