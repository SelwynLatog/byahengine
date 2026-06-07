#pragma once
#include "../tricycle/driver_model.hpp"

// per-species anim functions
// each takes the shared DriverModel bone system and writes pose.local per bone
// anim_timer accumulates in npc_update, speed is current move speed
// all functions write into a DriverPose so npc_draw can consume it directly

// indices used by animal anims 
// same bone slots, different semantic meaning per species
// CHICKEN: TORSO=body HEAD=head LEG_L=left leg LEG_R=right leg ARM_L=left wing ARM_R=right wing
// COW:     TORSO=body HEAD=head LEG_L=FL leg  LEG_R=FR leg  ARM_L=RL leg  ARM_R=RR leg
// CAT/DOG: TORSO=body HEAD=head LEG_L=FL leg  LEG_R=FR leg  ARM_L=RL leg  ARM_R=RR leg

void animal_anim_chicken(DriverPose& pose, float anim_timer, float speed, bool fleeing);
void animal_anim_cow(DriverPose& pose, float anim_timer, float speed, bool fleeing, bool grazing);
void animal_anim_cat(DriverPose& pose, float anim_timer, float speed, bool fleeing);
void animal_anim_dog(DriverPose& pose, float anim_timer, float speed, bool fleeing);