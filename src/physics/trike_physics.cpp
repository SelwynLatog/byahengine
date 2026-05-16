#include "trike_physics.hpp"
#include "../core/const.hpp"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

void trike_physics_update(TrikeState& state, const TrikeInput& input, float dt){
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

    float net_force = engine + brake_force + reverse_force + friction;
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

        // roll dynamics
        // lateral acceleration is what tips the trike
        // a = v^2 / R, R = wheelbase / tan(steer_angle)
        float lateral_accel_g= 0.0f;
        if (std::abs(state.steer_angle) > 0.001f && std::abs(state.speed) >= 5.5f){
            float turn_radius= Const::TRIKE_WHEELBASE / std::tan(std::abs(state.steer_angle));
            lateral_accel_g= (state.speed * state.speed) / turn_radius/ Const::GRAVITY;

            // sign
            // right turn= positive roll tips right
            // left turn= negative roll tips left
            if (state.steer_angle < 0.0f) lateral_accel_g = -lateral_accel_g;
        }

        // sidecar asymmetry= sidecar on the right resists right rolls & amplifies left rolls
        float sidecar_bias= (lateral_accel_g > 0.0f) ? 0.7f : 1.15f;
        lateral_accel_g*= sidecar_bias;

        // torque trying to tip the trike
        // lateral_accel * CG_height * total_mass
        float tip_torque= lateral_accel_g * Const::TRIKE_CG_HEIGHT;

        // restoring torque from suspension + gravity
        // pulls roll back to 0
        float restore= -state.roll_angle * Const::TRIKE_ROLL_STIFFNESS;

        // damping bleeds off roll oscillation
        float damping= -state.roll_rate * Const::TRIKE_ROLL_DAMPING;

        if (!state.is_tipping){
            float roll_accel= tip_torque + restore + damping;
            state.roll_rate+= roll_accel * dt;
            state.roll_angle+= state.roll_rate * dt;
        }

        // rollover check
        if (!state.is_tipping && std::abs(state.roll_angle) >= glm::radians(Const::TRIKE_ROLLOVER_THRESHOLD)) {
            state.is_tipping= true;
            state.tumble_vel  = forward * state.speed + right * state.lateral_speed;
            state.speed= 0.0f;
            printf("[TIPPING] roll=%.2f rate=%.2f\n", state.roll_angle, state.roll_rate);
        }

        // goofy tipping animation
        // past threshold gravity takes over
        // you roll goofily
        if (state.is_tipping){
            float fall_dir= (state.roll_angle> 0.0f) ? 1.0f : -1.0f;

            // trike slides in the direction it was rolling when it tipped
            // bleeds off over time simulating friction with the ground
            state.tumble_vel *= (1.0f - 2.5f * dt);
            state.position   += state.tumble_vel * dt;

            // gravity keeps accelerating the spin
            state.roll_rate+= fall_dir * 5.0f * dt;

            // bleed off spin rate over time so it eventually stops
            state.roll_rate*= (1.0f - 0.12f * dt);

            state.roll_angle += state.roll_rate * dt;

            // wrap angle so it spins full 360s visually
            state.roll_angle= std::remainder(state.roll_angle, glm::two_pi<float>());

            // keep on ground lvl
            state.position.y = 0.0f;

            // stop when spin rate is nearly dead
            if (std::abs(state.roll_rate) < 0.08f) {
                state.roll_angle     = 0.0f;
                state.roll_rate      = 0.0f;
                state.is_rolled_over = true;
                state.is_tipping     = false;
                state.rollover_timer = 0.0f;
            }

        }
}