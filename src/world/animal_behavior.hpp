#pragma once
#include "../world/npc.hpp"

// per-species behavior constants
// one row per NpcType, indexed directly by the enum value
// add a new species = add a row here + anim function in animal_anim.cpp
struct AnimalBehavior {
    float walk_speed;       // normal patrol speed m/s
    float flee_speed;       // speed when startled
    float startle_range;    // distance from trike that triggers flee
    float startle_duration; // seconds before returning to idle/walk
    float idle_time_min;    // min seconds to stand still between walks
    float idle_time_max;    // max seconds to stand still between walks
    float weight;           // kg, used if ever a passenger (easter egg)
    bool flocks;            // true = steers toward nearby same-type npcs
    bool grazes;            // true = plays graze anim during idle
    bool can_hail;          // true = eligible for para po easter egg
};

// indexed by NpcType enum
// NPC_TYPE_PERSON is not an animal, slot left as zero so indexing stays clean
static constexpr AnimalBehavior ANIMAL_BEHAVIOR_TABLE[] = {
    // PERSON not used by animal system, zeroed out
    { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false, false },

    // CHICKEN skittish, flocks, light, easter egg
    { 1.2f, 4.5f, 3.5f, 6.0f, 1.0f, 4.0f, 1.8f, true,  false, true  },

    // COW  slow, calm, grazes, heavy, hard to startle
    { 0.6f, 2.0f, 6.0f, 4.0f, 4.0f, 10.0f, 400.0f, false, true, false },

    // CAT medium speed, flees fast, solitary, unpredictable idle
    { 1.0f, 5.5f, 2.5f, 8.0f, 2.0f, 8.0f, 4.0f, false, false, false },

    // DOG faster walk, moderate startle, may follow trike briefly
    { 1.6f, 6.0f, 4.0f, 5.0f, 1.0f, 5.0f, 15.0f, false, false, false },

    // TIKBALANG placeholder
    { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false, true },
};

// convenience returns true if this npc type is an animal
inline bool npc_type_is_animal(NpcType t) {
    return t == NPC_TYPE_CHICKEN
        || t == NPC_TYPE_COW
        || t == NPC_TYPE_CAT
        || t == NPC_TYPE_DOG;
}