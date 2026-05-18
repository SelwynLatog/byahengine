#pragma once

namespace Const{
    // window
    inline constexpr int WINDOW_WIDTH = 1920;
    inline constexpr int WINDOW_HEIGHT= 1080;
    inline constexpr const char* WINDOW_TITLE="tricyce sim";
    inline constexpr bool VSYNC = true;

    // render
    inline constexpr float CLEAR_R = 0.12f;
    inline constexpr float CLEAR_G = 0.12f;
    inline constexpr float CLEAR_B = 0.12f;

    // physics
    inline constexpr float GRAVITY= 9.81f; // m/s2
    inline constexpr float FIXED_TIMESTEP= 1.0f/ 120.0f; // physics ticks at 120hz
    inline constexpr float MAX_DELTA= 0.05f; //clamp dt to avoid spiral of death!

    // tricycle
    inline constexpr float TRIKE_MASS = 180.0f; // kg + rider
    inline constexpr float TRIKE_MAX_SPEED = 12.0f; // m/s (43 km/h)
    inline constexpr float TRIKE_ENGINE_FORCE = 300.0f; // N
    inline constexpr float TRIKE_BRAKE_FORCE= 600.0f; // N
    inline constexpr float TRIKE_FRICTION = 8.5f; // rolling resistance coefficient
    inline constexpr float TRIKE_MAX_STEER_ANGLE = 20.0f; // degrees
    inline constexpr float TRIKE_STEER_SPEED = 90.0f; //degrees /sec
    inline constexpr float TRIKE_WHEELBASE = 1.8f; // metres, front to rear angle

    // rollover & lateral dynamics
    inline constexpr float TRIKE_CG_HEIGHT= 0.65f; // metres, center of gravity above ground
    inline constexpr float TRIKE_TRACK_WIDTH= 1.20f; // metres, mc wheel to sidecar wheel
    inline constexpr float TRIKE_SIDECAR_MASS= 60.0f; // kg, sidecar estimate depends some regions have bulkier models
    // but for our place typically pretty light

    inline constexpr float TRIKE_LATERAL_FRICTION= 12.0f; // resists sideways slip N/(m/s)
    inline constexpr float TRIKE_ROLL_STIFFNESS= 28.0f; // suspension spring restoring roll
    inline constexpr float TRIKE_ROLL_DAMPING= 14.0f; // damping on roll oscillation
    inline constexpr float TRIKE_ROLLOVER_THRESHOLD= 52.0f; // degrees, tip point
    inline constexpr float TRIKE_RESPAWN_DELAY= 2.5f; // seconds before reset after tip

    // Mesh default front
    // Some 3d models are stupidly set to side as their forward
    // the forward is towards Z axis
    // in this case, the model's def font is on the LEFT
    // nevermind my stupid ahh can just rotate the model in blender
    inline constexpr float TRIKE_MODEL_YAW_OFFSET = 0.0f; // degrees: OBJ forward vs +X axis

    // mesh moddle toggle
    // true - use pure coded mesh at tricycle_mesh.cpp
    // false - use OBJ file at TRIKE_MODEL_PATH
    // no OBJ means I can determine if directional issues are physics or OBJ related visually
    inline constexpr bool USE_PROC_MESH = false;
    // tricycle model path
    inline constexpr const char* TRIKE_MODEL_PATH = "../assets/TRAYSIKEL.obj";

    // cam
    inline constexpr float CAM_YAW_DEFAULT= 0.0f;
    inline constexpr float CAM_PITCH_DEFAULT= 25.0f;
    inline constexpr float CAM_DIST_DEFAULT= 5.0f;
    inline constexpr float CAM_PITCH_MIN= 5.0f;
    inline constexpr float CAM_PITCH_MAX= 85.0f;
    inline constexpr float CAM_YAW_SPEED= 60.0f;  // degrees/sec
    inline constexpr float CAM_PITCH_SPEED= 40.0f;  // degrees/sec
    inline constexpr float CAM_FOV= 55.0f;  // degrees
    inline constexpr float CAM_NEAR= 0.1f;
    inline constexpr float CAM_FAR= 200.0f;
    inline constexpr float CAM_ORBIT_TARGET_Y= 0.5f;
    inline constexpr float CAM_LERP_SPEED= 5.0f; // how fast cam catches to trike
    inline constexpr float CAM_LOOKAHEAD= 1.5f; //metres ahead of trike at max speed

    // editor
    inline constexpr float EDITOR_CAM_SPEED = 12.0f; //metres/sec freecam movement
    inline constexpr float EDITOR_LOOK_SENSITIVITY = 0.003f; // radians per pixel drag
    inline constexpr float EDITOR_GRID_SNAP = 0.5f; // metres, placement snaps to this grid
    inline constexpr float EDITOR_ROTATE_SPEED = 1.5f; // radians/sec
    inline constexpr float EDITOR_SCALE_SPEED = 0.5f; // units/sec
    inline constexpr const char* MAP_SAVE_PATH = "../assets/map.txt";
    inline constexpr int EDITOR_GRID_RADIUS = 50;  // grid lines extend 50m from origin
    inline constexpr int EDITOR_PAGE_SIZE = 9; // num of prop selection

    // lighting
    inline constexpr float LIGHT_DIR_X= 1.0f;
    inline constexpr float LIGHT_DIR_Y= 2.0f;
    inline constexpr float LIGHT_DIR_Z= 1.0f;
    inline constexpr float LIGHT_AMBIENT= 0.55f;
    inline constexpr float LIGHT_DIFF= 0.85f;

    // ground & terrain
    inline constexpr float GROUND_HALF_EXTENT= 200.0f;
    inline constexpr float GROUND_Y_OFFSET= -0.002f;
    inline constexpr float GROUND_KD= 0.18f;   // uniform RGB, dark asphalt

    // model
    inline constexpr float MODEL_NORMALIZE_SIZE= 2.0f;  // auto-scale longest axis to this (metres)
    inline constexpr float MODEL_FLOOR_FUDGE= 0.03f; // nudge model down so wheels kiss ground

    // ground grid
    inline constexpr float GROUND_GRID_TILE_SIZE= 4.0f;
    inline constexpr float GROUND_KD_ALT= 0.22f;

    // axis gizmo cords
    inline constexpr float GIZMO_LENGTH = 3.0f;  // metres

    // restitution - small bounce back along normal
    // 0.18 fairly dead collision
    // feels heavy not pinball
    inline constexpr float RESTITUTION= 0.18f;

    // DYNAMIC PROPS

    // traffic cones - check
    inline constexpr float DYN_CONE_MASS = 3.0f;
    inline constexpr float DYN_CONE_RESTITUTION = 0.60f;
    inline constexpr float DYN_CONE_FRICTION = 0.70f;

    // garbage bins - check
    inline constexpr float DYN_BIN_MASS = 12.0f;
    inline constexpr float DYN_BIN_RESTITUTION = 0.45f;
    inline constexpr float DYN_BIN_FRICTION = 0.70f;

    // garbage bags - check
    inline constexpr float DYN_BAG_MASS = 1.5f;
    inline constexpr float DYN_BAG_RESTITUTION = 0.20f;
    inline constexpr float DYN_BAG_FRICTION = 0.95f;

    // parked motorcycles - check
    inline constexpr float DYN_MOTORCYCLE_MASS = 120.0f;
    inline constexpr float DYN_MOTORCYCLE_RESTITUTION = 0.20f;
    inline constexpr float DYN_MOTORCYCLE_FRICTION = 0.85f;

    // parked trikes - check
    inline constexpr float DYN_TRIKE_MASS = 180.0f;
    inline constexpr float DYN_TRIKE_RESTITUTION = 0.15f;
    inline constexpr float DYN_TRIKE_FRICTION = 0.90f;

    // food carts
    // ice cream, isawan, fishballan, etc
    inline constexpr float DYN_CART_MASS = 35.0f;
    inline constexpr float DYN_CART_RESTITUTION = 0.30f;
    inline constexpr float DYN_CART_FRICTION = 0.60f; 

    // poles and posts
    // street lamps, sign posts
    inline constexpr float DYN_POLE_MASS = 18.0f;
    inline constexpr float DYN_POLE_RESTITUTION = 0.25f;
    inline constexpr float DYN_POLE_FRICTION = 0.90f;

    // food stalls
    // fruit stands, palengke stalls
    inline constexpr float DYN_STALL_MASS = 25.0f;
    inline constexpr float DYN_STALL_RESTITUTION = 0.15f;
    inline constexpr float DYN_STALL_FRICTION = 0.70f;

    // barrells
    inline constexpr float DYN_BARREL_MASS = 20.0f;
    inline constexpr float DYN_BARREL_RESTITUTION = 0.55f;
    inline constexpr float DYN_BARREL_FRICTION = 0.65f;

    // railings
    inline constexpr float DYN_RAILING_MASS = 40.0f;
    inline constexpr float DYN_RAILING_RESTITUTION = 0.20f;
    inline constexpr float DYN_RAILING_FRICTION = 0.85f;

    // 4 wheelers. trucks, suvs
    inline constexpr float DYN_CAR_MASS = 1100.0f;
    inline constexpr float DYN_CAR_RESTITUTION = 0.12f;
    inline constexpr float DYN_CAR_FRICTION = 0.90f;

    inline constexpr float DYN_TRUCK_MASS = 8000.0f;
    inline constexpr float DYN_TRUCK_RESTITUTION = 0.08f;
    inline constexpr float DYN_TRUCK_FRICTION = 0.95f;

    // individual fruits
    inline constexpr float DYN_FRUIT_MASS = 0.3f;
    inline constexpr float DYN_FRUIT_RESTITUTION = 0.70f;
    inline constexpr float DYN_FRUIT_FRICTION = 0.60f;

    // fallback
    inline constexpr float DYN_DEFAULT_MASS = 8.0f;
    inline constexpr float DYN_DEFAULT_RESTITUTION = 0.55f;
    inline constexpr float DYN_DEFAULT_FRICTION = 0.75f;

}