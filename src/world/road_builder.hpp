#pragma once
#include <string>

// generates flat road segment OBJ+MTL files into assets/props at startup
// skips files that already exist so it doesn't overwrite on every run
// each segment is a 4x4m quad at y=0 with tiling UVs
void road_builder_init(const std::string& assets_dir);