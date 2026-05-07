#pragma once
#include <glm/glm.hpp>

// axis-aligned bounding box in world space
// recomputed every frame from trike position + heading
// intentionally kept as a simple AABB (not OBB) for now
// good enough for collision detection on a flat ground plane
// OBB upgrade can come later if needed
struct Aabb {
    glm::vec3 min= glm::vec3(0.0f);
    glm::vec3 max= glm::vec3(0.0f);

    // center and half-extents for convenience
    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 half_extents() const { return (max - min) * 0.5f; }
};

// half-extents of the trike in its LOCAL frame (metres)
// these are fixed geometry constants — tune to match the model visually
// x= half-length (front to back), z = half-width (side to side), y = half-height
static constexpr float TRIKE_HEXT_X= 0.85f;  // ~2.2m bumper to bumper
static constexpr float TRIKE_HEXT_Y= 0.75f; // ~1.5m tall with canopy
static constexpr float TRIKE_HEXT_Z= 0.50f; // ~1.3m wide including sidecar

// recomputes the world-space AABB from the trike's current position and heading
// called once per frame after physics update, before any collision checks
// because this is an AABB (not OBB) we take the worst-case envelope of the
// rotated box corners 
//the box will be slightly loose on diagonals
void aabb_update(Aabb& box, const glm::vec3& position, float heading);

// returns true if two AABBs overlap on all three axes
bool aabb_overlap(const Aabb& a, const Aabb& b);

// returns the minimum translation vector to push 'a' out of 'b'
// only valid when aabb_overlap returns true
// direction is the axis with the smallest penetration depth
glm::vec3 aabb_mtv(const Aabb& a, const Aabb& b);