#include "trike_physics.hpp"
#include "../core/const.hpp"
#include "../world/height_field.hpp"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

void trike_physics_update(TrikeState& state, const TrikeInput& input, const HeightField& terrain, float dt){
    // if tipped over, count down respawn timer then reset
    if (state.is_rolled_over){
        state.rollover_timer+= dt;
        if (state.rollover_timer>= Const::TRIKE_RESPAWN_DELAY){
            state.position= glm::vec3(0.0f);
            state.heading= 0.0f;
            state.tumble_vel = glm::vec3(0.0f);
            state.speed= 0.0f;
            state.lateral_speed= 0.0f;
            state.steer_angle= 0.0f;
            state.velocity_heading= 0.0f;
            state.roll_angle= 0.0f;
            state.roll_rate= 0.0f;
            state.is_rolled_over= false;
            state.is_tipping= false;
            state.rollover_timer= 0.0f;
            state.is_airborne = false;
            state.vert_vel = 0.0f;
            state.susp_vel = 0.0f;
            state.air_pitch_vel  = 0.0f;
            state.air_roll_vel   = 0.0f;
            state.air_time       = 0.0f;
            state.tumble_pitch       = 0.0f;
            state.tumble_pitch_rate  = 0.0f;
        }
        return;
    }

    // Steer
    // movement current stear angle toward the target at a fixed rate
    float steer_target = input.steer * glm::radians(Const::TRIKE_MAX_STEER_ANGLE);
    float steer_delta = glm::radians(Const::TRIKE_STEER_SPEED) * dt;
    float steer_diff = steer_target - state.steer_angle;

    // clamp the step so we can never overshoot the target
    state.steer_angle  += std::clamp(steer_diff, -steer_delta, steer_delta);

    // Longitudinal forces
    // tight turn basically steals engine power
    // full lock = -30% force if you're wondering about the float vals
    float steer_load = 1.0f - 0.3f * std::abs(state.steer_angle) / glm::radians(Const::TRIKE_MAX_STEER_ANGLE);
    float engine = input.throttle * Const::TRIKE_ENGINE_FORCE * steer_load;
    float brake = input.brake * Const::TRIKE_BRAKE_FORCE;

    // brake always opposes current motion
    // at standstill, brake becomes reverse
    // for now there is of course no driver model, so it looks goofy
    // but realistically this is the only way to fix the problem of
    // toggling between "sliding off" a static mesh or
    // getting hard stuck post collision and not being able to get off
    
    // brake only opposes forward motion, doesn't fight reverse
    float brake_force = (state.speed > 0.0f) ? -input.brake * Const::TRIKE_BRAKE_FORCE : 0.0f;
    // reverse is a separate gentle push backward (speed clamped to 30% max below)
    float reverse_force = -input.reverse * Const::TRIKE_ENGINE_FORCE * 0.45f;
    // rolling friction always opposes motion, reduced during braking/reversing so they aren't fighting it
    float friction = (input.brake == 0.0f && input.reverse == 0.0f)
        ? -state.speed * Const::TRIKE_FRICTION : -state.speed * Const::TRIKE_FRICTION * 0.4f;

    float slope = state.slope_force * Const::TRIKE_MASS;
    float net_force = engine + brake_force + reverse_force + friction + slope;
    
    float acceleration = net_force / Const::TRIKE_MASS;
    state.speed += acceleration * dt;

    // clamp to max speed in both directions
    state.speed = std::clamp(state.speed, 
        -Const::TRIKE_MAX_SPEED * 0.3f, // reverse is limited to 30% of max
        Const::TRIKE_MAX_SPEED);

        // dead stop : prevent creeping at near-zero speed with no input
        if (input.throttle == 0.0f && input.brake == 0.0f && input.reverse == 0.0f && std::abs(state.speed) < 0.05f)
            state.speed = 0.0f;

        // Bicycle steering model
        // turning radius from wheelbase and steer angle
        // R = wheelbase/ tan(steer_angle)
        // heading change per second = speed/ R = speed * tan(steer_engle)/ wheelbase
        if (std::abs(state.steer_angle)> 0.001f){
            float turn_rate = (state.speed * std::tan(state.steer_angle))/ Const::TRIKE_WHEELBASE;
            if (state.speed < 0.0f) turn_rate = -turn_rate;

            // speed sensitivity- prevent trike from spinning like a beyblade at same speeds
            // low speed turn is tight
            // high speed turn is very loose
            // dividing by speed-based factor prevents beyblade spinning at low speed
            float speed_factor = 1.0f + (state.speed * state.speed) * 0.04f;
            turn_rate /= speed_factor;

            // sidecar drag:: asymmetric resistance
            // sidecar is on the right (+Z), so left turns (positive turn_rate) are harder
            float sidecar_resist = (turn_rate > 0.0f) ? 0.6f : 1.0f;
            turn_rate *= sidecar_resist;

            state.heading += turn_rate * dt;
        }
        
        // wrap heading to [-pi, pi]
        state.heading = std::remainder(state.heading, 2.0f * glm::pi<float>());

        // blend velocity heading toward body heading
        // creates organic lag on cornering
        float slip_angle = std::remainder(state.heading - state.velocity_heading, 2.0f * glm::pi<float>());
        state.velocity_heading += slip_angle * std::min(1.0f, 8.0f * dt);
        
        //integrate position
        glm::vec3 forward = {
            std::cos(state.velocity_heading),
            0.0f,
            std::sin(state.velocity_heading)
        };

        state.position += forward * state.speed * dt;

        // lateral dynamics
        // compute local right vector
        // perpendicular to heading, in XZ plane
        glm::vec3 right ={
            std::cos(state.heading + glm::half_pi<float>()),
            0.0f,
            std::sin(state.heading + glm::half_pi<float>())
        };

        // lateral friction bleeds off sideways slip each tick
        float lateral_friction= -state.lateral_speed * Const::TRIKE_LATERAL_FRICTION;
        float lateral_accel= lateral_friction/ Const::TRIKE_MASS;
        state.lateral_speed+= lateral_accel * dt;

        // cornering builds lateral slip proportional to turn rate and speed
        // feeds actual slip into the system
        // trike roll won't physically drift outward during hard turns without this
        float slip_input = (state.speed * std::tan(state.steer_angle)) * 0.35f;
        if (std::abs(state.speed) > 0.1f) state.lateral_speed += slip_input * dt;

        // dead stop lateral creep
        if (std::abs(state.lateral_speed) < 0.05f) state.lateral_speed = 0.0f;
        // integrate lateral pos
        state.position += right * state.lateral_speed * dt;

        // airborne vs grounded
        // sample ground and decide which regime we're in
        float ground_y = heightfield_sample(terrain, state.position.x, state.position.z);
        float target_y = ground_y + Const::TRIKE_SUSP_REST;
        float air_gap = state.position.y - target_y; // how far above rest position

        // gentle re-contact (air_gap closed naturally from above, not through floor)
        // hard landings are handled inside the airborne block
        // low-pass the susp_vel to smooth out rapid terrain noise
        // alpha controls cutoff: lower = smoother, higher = more responsive
        // 0.15 keeps the feel without the seizure
        static float susp_vel_smooth = 0.0f;
        float susp_alpha = 0.15f;
        susp_vel_smooth = susp_vel_smooth + susp_alpha * (state.susp_vel - susp_vel_smooth);
        state.susp_vel = susp_vel_smooth;

        if (state.is_airborne && air_gap <= 0.05f && state.vert_vel >= 0.0f){
            state.last_ground_y = 0.0f;
            state.is_airborne = false;
            state.susp_vel = state.vert_vel;
            state.vert_vel = 0.0f;
            state.air_pitch_vel = 0.0f;
            state.air_roll_vel = 0.0f;
            state.air_time = 0.0f;
            state.position.y = target_y;
        }

        // left the ground this frame: launch
        if (!state.is_airborne && air_gap > Const::TRIKE_SUSP_DROOP + 0.05f){
            state.is_airborne = true;
            state.vert_vel = state.susp_vel;
            state.susp_vel = 0.0f;
            state.air_time = 0.0f;

            // seed airborne tumble from launch conditions
            // faster speed + steeper pitch = more uncontrolled tumble in the air
            float launch_speed = std::abs(state.speed);
            float pitch_chaos = state.pitch_angle * launch_speed * 0.08f;
            float roll_chaos = state.roll_angle  * launch_speed * 0.06f;
            state.air_pitch_vel = pitch_chaos;
            state.air_roll_vel = roll_chaos + (state.roll_rate * 0.4f); // carry roll momentum into air
        }

        // bump detection via local height variance
        // bilinear sampling smooths out bumps so delta-per-frame is useless
        // instead sample the 4 raw cell corners around the trike
        // high variance between them = rough ground = punch the suspension
        // this correctly reads actual terrain bumpiness regardless of speed
        float s = terrain.cell_size;
        float h_fwd  = heightfield_sample(terrain, state.position.x + s, state.position.z);
        float h_back = heightfield_sample(terrain, state.position.x - s, state.position.z);
        float h_left = heightfield_sample(terrain, state.position.x, state.position.z - s);
        float h_right= heightfield_sample(terrain, state.position.x, state.position.z + s);

        float h_avg  = (h_fwd + h_back + h_left + h_right) * 0.25f;
        float variance = (std::abs(h_fwd  - h_avg)
                        + std::abs(h_back - h_avg)
                        + std::abs(h_left - h_avg)
                        + std::abs(h_right- h_avg)) * 0.25f;

        // only excite suspension when variance is meaningful
        // speed scales it
        float bump_threshold = 0.08f;
        if (!state.is_airborne && variance > bump_threshold){
            float speed_scale = glm::clamp(std::abs(state.speed) / 6.0f, 0.2f, 2.5f);
            // randomize sign each frame so it wobbles rather than always pushing one way
            // use position as a cheap deterministic noise source
            float sign = (std::fmod(state.position.x + state.position.z, 0.2f) > 0.1f) ? 1.0f : -1.0f;
            float punch = sign * variance * speed_scale * 18.0f;
            state.susp_vel += punch;
        }

        float ground_delta  = ground_y - state.last_ground_y;
        state.last_ground_y = ground_y;
        if (!state.is_airborne && std::abs(ground_delta) > 0.04f){
            float speed_scale = glm::clamp(std::abs(state.speed) / 8.0f, 0.3f, 2.0f);
            state.susp_vel += -ground_delta * speed_scale * 22.0f;
        }

        if (state.is_airborne){
            state.air_time += dt;

            // free-fall
            state.vert_vel -= Const::GRAVITY * dt;
            state.position.y += state.vert_vel * dt;

            // mid-air tumble
            // damp slightly over time so it doesn't go full beyblade forever

            state.air_pitch_vel *= (1.0f - 0.3f * dt);
            state.air_roll_vel *= (1.0f - 0.2f * dt);
            state.pitch_angle += state.air_pitch_vel * dt;
            state.roll_angle += state.air_roll_vel  * dt;


            if (state.position.y < target_y){
                state.position.y = target_y;

                // impact severity based on how long we were airborne + fall velocity
                float impact_vel = std::abs(state.vert_vel);
                float severity = impact_vel * state.air_time; // fast fall + long air = brutal

                state.last_impact_force = impact_vel * Const::TRIKE_MASS;

                if (severity > 4.5f){
                    // hard landing causes tip into tumble
                    state.is_tipping = true;
                    state.is_airborne = false;
                    state.tumble_vel = forward * state.speed + glm::vec3(0.0f, state.vert_vel * 0.3f, 0.0f);
                    state.roll_rate = state.air_roll_vel * 0.4f + (impact_vel * 0.2f * (state.roll_angle >= 0.0f ? 1.0f : -1.0f));
                    // cliff landing pitches nose hard into ground
                    state.tumble_pitch_rate = state.air_pitch_vel * 0.3f + impact_vel * 0.15f;
                    state.tumble_pitch = state.pitch_angle;
                    state.speed = 0.0f;
                    state.vert_vel = 0.0f;
                    state.air_time = 0.0f;
                } 
                else {
                    // soft/medium landing then suspension absorbs it
                    state.is_airborne = false;
                    state.susp_vel = state.vert_vel; // punch the spring
                    state.vert_vel = 0.0f;
                    state.air_time = 0.0f;
                    // bleed out the air tumble on landing
                    state.air_pitch_vel = 0.0f;
                    state.air_roll_vel  = 0.0f;
                }
            }

            state.susp_offset = 0.0f;
        } 
        else {
            // grounded: normal spring-damper
            float disp = state.position.y - target_y;
            float spring_f = -Const::TRIKE_SUSP_STIFFNESS * disp;
            float damper_f = -Const::TRIKE_SUSP_DAMPING   * state.susp_vel;
            float susp_accel = (spring_f + damper_f) / Const::TRIKE_MASS;

            state.susp_vel += susp_accel * dt;
            state.position.y += state.susp_vel * dt;

            // clamp travel
            float susp_travel = state.position.y - target_y;
            if (susp_travel < -Const::TRIKE_SUSP_BUMP){
                state.position.y = target_y - Const::TRIKE_SUSP_BUMP;
                state.susp_vel   = std::max(0.0f, state.susp_vel);
            }
            if (susp_travel > Const::TRIKE_SUSP_DROOP){
                state.position.y = target_y + Const::TRIKE_SUSP_DROOP;
                state.susp_vel = std::min(0.0f, state.susp_vel);
            }
            state.susp_offset = state.position.y - target_y;
        }

        // idle body bob 
        // engine vibration at low speed
        // accumulates as a phase, applied as Y offset in the renderer
        float idle_t = 1.0f - glm::clamp(std::abs(state.speed) / 6.0f, 0.0f, 1.0f);
        float bob_freq = 28.0f + std::abs(state.speed) * 0.6f; // ~28hz at idle

        static float bob_phase  = 0.0f;
        static float bob_phase2 = 1.3f; // offset so X and Y aren't in sync
        static float bob_phase3 = 2.7f; // Z offset

        bob_phase  += bob_freq* dt;
        bob_phase2 += bob_freq * 0.7f * dt; // X shakes at a different harmonic
        bob_phase3 += bob_freq * 1.3f * dt; // Z is a higher harmonic

        // Y bob: smooth, the main vertical engine pulse
        state.body_bob = std::sin(bob_phase) * 0.004f * idle_t;

        // XZ shake: chassis rattling
        // rattle as micro-rotations
        // two beating harmonics per axis = uneven single-cylinder character
        state.shake_pitch = (std::sin(bob_phase2) * 0.6f + std::sin(bob_phase2 * 2.1f) * 0.4f) * 0.003f * idle_t;
        state.shake_roll = (std::sin(bob_phase3) * 0.5f + std::sin(bob_phase3 * 1.7f) * 0.5f) * 0.002f * idle_t;

        // surface normal + pitch angle
        state.surface_normal = heightfield_normal(terrain, state.position.x, state.position.z);

        // pitch is the angle between the surface normal and world up, projected onto heading
        glm::vec3 fwd_flat = glm::vec3(std::cos(state.heading), 0.0f, std::sin(state.heading));
        float slope_along_fwd = glm::dot(state.surface_normal, fwd_flat);
        state.pitch_angle = std::asin(glm::clamp(-slope_along_fwd, -1.0f, 1.0f));

        // slope force
        // gravity component pulling along forward axis
        // negative = going uphill (drag)
        // positive = going downhill (free acceleration)
         state.slope_force = -Const::GRAVITY * Const::TERRAIN_SLOPE_GRAVITY_SCALE
            * std::sin(state.pitch_angle);

        // cross-slope: how much the terrain tilts the trike sideways
        // project surface normal onto the local right vector
        // positive = terrain slopes right = tips right
        glm::vec3 right_flat = glm::vec3(
            std::cos(state.heading + glm::half_pi<float>()),
            0.0f,
            std::sin(state.heading + glm::half_pi<float>()));
        float cross_slope = glm::dot(state.surface_normal, right_flat);
        // convert to a gravity-driven lateral accel equivalent
        // sin(cross_angle) * g, normalized by g to stay in accel_g units
        float slope_lateral_g = cross_slope * Const::GRAVITY / Const::GRAVITY; // = cross_slope scalar

        // roll dynamics
        // two sources of tip torque: cornering + side-slope gravity
        float lateral_accel_g = 0.0f;
        if (std::abs(state.steer_angle) > 0.001f && std::abs(state.speed) >= 3.5f){
            float turn_radius = Const::TRIKE_WHEELBASE / std::tan(std::abs(state.steer_angle));
            lateral_accel_g = (state.speed * state.speed) / turn_radius / Const::GRAVITY;
            if (state.steer_angle < 0.0f) lateral_accel_g = -lateral_accel_g;
        }

        // sidecar asymmetry
        // sidecar on right resists rightward roll, amplifies left
        float sidecar_bias = (lateral_accel_g > 0.0f) ? 0.7f : 1.15f;
        lateral_accel_g   *= sidecar_bias;

        // slope adds directly
        // scale by CG height to convert accel to torque
        float tip_torque = (lateral_accel_g + slope_lateral_g * 2.2f) * Const::TRIKE_CG_HEIGHT;

        // restoring torque
        float restore = -state.roll_angle * Const::TRIKE_ROLL_STIFFNESS * 0.4f;

        // damping
        float damping = -state.roll_rate * Const::TRIKE_ROLL_DAMPING;

        if (!state.is_tipping){
            float roll_accel = tip_torque + restore + damping;
            state.roll_rate += roll_accel * dt;
            state.roll_angle+= state.roll_rate * dt;
        }

        // rollover check
        if (!state.is_tipping && std::abs(state.roll_angle) >= glm::radians(Const::TRIKE_ROLLOVER_THRESHOLD)){
            state.is_tipping = true;
            state.tumble_vel = forward * state.speed + right * state.lateral_speed;
            // pitch rate seeded from forward speed
            state.tumble_pitch_rate = state.speed * 0.12f;
            state.tumble_pitch = state.pitch_angle; // start from current nose angle
            state.roll_rate = glm::clamp(state.roll_rate, -6.0f, 6.0f);
            state.speed = 0.0f;
        }

        // tumble
        if (state.is_tipping){
            glm::vec3 gravity_world = glm::vec3(0.0f, -Const::GRAVITY, 0.0f);
            glm::vec3 slope_pull = gravity_world - state.surface_normal * glm::dot(gravity_world, state.surface_normal);
            float slope_steepness = glm::length(slope_pull) / Const::GRAVITY; // 0..1

            // slide down slope
            state.tumble_vel += slope_pull * 0.35f * dt;
            state.tumble_vel *= (1.0f - 1.8f * dt);
            state.position += state.tumble_vel * dt;

            // roll axis
            float fall_dir = (state.roll_angle >= 0.0f) ? 1.0f : -1.0f;
            float rolls_done = std::abs(state.roll_angle) / glm::two_pi<float>();
            float gravity_fade = glm::clamp(1.0f - rolls_done * 1.2f, 0.0f, 1.0f);
            float spin_gravity = (2.5f + slope_steepness * 4.0f) * gravity_fade;
            state.roll_rate += fall_dir * spin_gravity * dt;

            // hard clamp
            state.roll_rate = glm::clamp(state.roll_rate, -9.5f, 9.5f);

            // heavy bleed
            // energy drains fast on flat, slower on steep slope
            float spin_bleed = 0.55f * (1.0f - slope_steepness * 0.5f);
            state.roll_rate *= (1.0f - spin_bleed * dt);
            state.roll_angle += state.roll_rate * dt;
            state.roll_angle = std::remainder(state.roll_angle, glm::two_pi<float>());

            // pitch axis: forward/backward tumble
            // much weaker than roll a trike's weight resists nose-over-tail
            // only slope steepness drives it, and it's clamped so it can't fish-flap
            float pitch_gravity = slope_steepness * 1.2f * (glm::dot(state.tumble_vel, forward) > 0.0f ? 1.0f : -1.0f);
            state.tumble_pitch_rate += pitch_gravity * dt;

            // hard clamp: pitch can wobble but never full continuous spin
            state.tumble_pitch_rate = glm::clamp(state.tumble_pitch_rate, -2.5f, 2.5f);
            state.tumble_pitch_rate *= (1.0f - 0.55f * dt); // bleeds fast — pitch dies before roll
            state.tumble_pitch += state.tumble_pitch_rate * dt;
            state.tumble_pitch = glm::clamp(state.tumble_pitch, -glm::half_pi<float>(), glm::half_pi<float>());

            state.is_airborne = false;
            state.vert_vel = 0.0f;
            state.position.y = heightfield_sample(terrain, state.position.x, state.position.z);

            // stop when both roll and pitch spin die on flat ground
            if (std::abs(state.roll_rate) < 0.08f
             && std::abs(state.tumble_pitch_rate) < 0.08f
             && slope_steepness < 0.15f){
                state.roll_angle = 0.0f;
                state.roll_rate = 0.0f;
                state.tumble_pitch = 0.0f;
                state.tumble_pitch_rate = 0.0f;
                state.is_rolled_over = true;
                state.is_tipping = false;
                state.rollover_timer = 0.0f;
            }
        }
}