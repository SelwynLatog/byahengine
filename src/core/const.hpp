#pragma once

namespace Const{
    // window
    inline constexpr int WINDOW_WIDTH = 1920;
    inline constexpr int WINDOW_HEIGHT= 1080;
    inline constexpr const char* WINDOW_TITLE="tricycle sim";
    inline constexpr bool VSYNC = false;

    // render
    inline constexpr float CLEAR_R = 0.12f;
    inline constexpr float CLEAR_G = 0.12f;
    inline constexpr float CLEAR_B = 0.12f;

    inline constexpr const char* SKY_IMAGE_PATH = "../assets/sky_day.jpg";

    // day cycle
    // 10 real minutes = 1 full in-game day
    inline constexpr float DAY_DURATION_SECONDS = 600.0f;
    inline constexpr float DAY_START_TIME = 7.0f; // start at 7am

    // time ranges (0-24)
    inline constexpr float DAY_MORNING_START = 5.0f;
    inline constexpr float DAY_AFTERNOON_START = 14.0f;
    inline constexpr float DAY_NIGHT_START = 19.0f;
    inline constexpr float DAY_FADE_DURATION = 1.5f; // hours to crossfade between periods

    // light color keyframes per period
    // morning: warm white sun
    inline constexpr float LIGHT_MORNING_R = 1.00f;
    inline constexpr float LIGHT_MORNING_G = 0.95f;
    inline constexpr float LIGHT_MORNING_B = 0.85f;
    inline constexpr float LIGHT_MORNING_AMBIENT = 0.50f;
    inline constexpr float LIGHT_MORNING_DIFF = 0.85f;

    // afternoon/golden hour: deep orange
    inline constexpr float LIGHT_AFTERNOON_R = 1.00f;
    inline constexpr float LIGHT_AFTERNOON_G = 0.55f;
    inline constexpr float LIGHT_AFTERNOON_B = 0.20f;
    inline constexpr float LIGHT_AFTERNOON_AMBIENT = 0.40f;
    inline constexpr float LIGHT_AFTERNOON_DIFF = 0.90f;

    // night: dim cool blue
    inline constexpr float LIGHT_NIGHT_R = 0.10f;
    inline constexpr float LIGHT_NIGHT_G = 0.15f;
    inline constexpr float LIGHT_NIGHT_B = 0.40f;
    inline constexpr float LIGHT_NIGHT_AMBIENT = 0.45f;
    inline constexpr float LIGHT_NIGHT_DIFF = 0.20f;

    // physics
    inline constexpr float GRAVITY= 9.81f; // m/s2
    inline constexpr float FIXED_TIMESTEP= 1.0f/ 120.0f; // physics ticks at 120hz
    inline constexpr float MAX_DELTA= 0.05f; //clamp dt to avoid spiral of death!

    // ***********************************
    // TRICYCLE
    //***********************************/
    inline constexpr float TRIKE_MASS = 180.0f; // kg + rider
    inline constexpr float TRIKE_MAX_SPEED = 12.0f; // 12 m/s (43 km/h)
    inline constexpr float TRIKE_ENGINE_FORCE = 300.0f; // N
    inline constexpr float TRIKE_BRAKE_FORCE= 600.0f; // N
    inline constexpr float TRIKE_FRICTION = 8.5f; // rolling resistance coefficient
    inline constexpr float TRIKE_MAX_STEER_ANGLE = 35.0f; // degrees
    inline constexpr float TRIKE_STEER_SPEED = 90.0f; //degrees /sec
    inline constexpr float TRIKE_WHEELBASE = 1.8f; // metres, front to rear angle

    // rollover & lateral dynamics
    inline constexpr float TRIKE_CG_HEIGHT= 0.65f; // metres, center of gravity above ground
    inline constexpr float TRIKE_TRACK_WIDTH= 1.20f; // metres, mc wheel to sidecar wheel
    inline constexpr float TRIKE_SIDECAR_MASS= 60.0f; // kg, sidecar estimate depends some regions have bulkier models
    // but for our place typically pretty light

    inline constexpr float TRIKE_LATERAL_FRICTION= 12.0f; // resists sideways slip N/(m/s)
    inline constexpr float TRIKE_ROLL_STIFFNESS= 31.0f; // suspension spring restoring roll
    inline constexpr float TRIKE_ROLL_DAMPING= 14.0f; // damping on roll oscillation
    inline constexpr float TRIKE_ROLLOVER_THRESHOLD= 28.0f; // degrees, tip point
    inline constexpr float TRIKE_RESPAWN_DELAY= 2.5f; // seconds before reset after tip

    // suspension
    static constexpr float TRIKE_SUSP_REST = 0.0f; // ride height above ground_y (wheel radius already baked into model offset)
    static constexpr float TRIKE_SUSP_STIFFNESS = 18000.0f; // N/m soft enough to bounce but stiff enough not to wallow
    static constexpr float TRIKE_SUSP_DAMPING = 900.0f; // N·s/m critically damped feel
    static constexpr float TRIKE_SUSP_BUMP = 0.12f; // max compression travel, meters
    static constexpr float TRIKE_SUSP_DROOP = 0.08f; // max droop travel, meters


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
    inline constexpr const char* TRIKE_PARTS_MODEL_PATH = "../assets/TRAYSIKEL_parts.obj";
    inline constexpr float TRIKE_WHEEL_RADIUS = 0.28f; // metres, tune to model

    // cam
    inline constexpr float CAM_YAW_DEFAULT= 0.0f;
    inline constexpr float CAM_PITCH_DEFAULT= 25.0f;
    inline constexpr float CAM_DIST_DEFAULT= 5.0f;
    inline constexpr float CAM_PITCH_MIN= 5.0f;
    inline constexpr float CAM_PITCH_MAX= 85.0f;
    inline constexpr float CAM_YAW_SPEED= 60.0f;  // degrees/sec
    inline constexpr float CAM_PITCH_SPEED= 40.0f;  // degrees/sec
    inline constexpr float CAM_FOV= 55.0f;  // degrees
    inline constexpr float CAM_NEAR= 0.5f;
    inline constexpr float CAM_FAR= 400.0f;
    inline constexpr float CAM_ORBIT_TARGET_Y= 0.5f;
    inline constexpr float CAM_LERP_SPEED= 5.0f; // how fast cam catches to trike
    inline constexpr float CAM_LOOKAHEAD= 1.5f; //metres ahead of trike at max speed
    inline constexpr float CAM_SLOPE_PITCH_SCALE  = 1.8f;  // how much slope tilts the cam, tune up/down
    inline constexpr float CAM_SLOPE_LERP_SPEED = 3.0f;  // how fast cam pitch chases slope change
    inline constexpr float CAM_SLOPE_TARGET_Y_BIAS = 1.2f; // metres lookat lifts per radian of uphill pitch

    // editor
    inline constexpr float EDITOR_CAM_SPEED = 30.0f; //metres/sec freecam movement
    inline constexpr float EDITOR_LOOK_SENSITIVITY = 0.003f; // radians per pixel drag
    inline constexpr float EDITOR_GRID_SNAP = 0.5f; // metres, placement snaps to this grid
    inline constexpr float EDITOR_ROTATE_SPEED = 1.5f; // radians/sec
    inline constexpr float EDITOR_SCALE_SPEED = 0.5f; // units/sec
    inline constexpr const char* MAP_SAVE_PATH = "../assets/map.txt";
    inline constexpr int EDITOR_GRID_RADIUS = 50;  // grid lines extend 50m from origin
    inline constexpr int EDITOR_PAGE_SIZE = 9; // num of prop selection
    inline constexpr float EDITOR_GRID_SNAP_FINE = 0.05f;  // alt+arrows: 5cm steps
    inline constexpr float EDITOR_SCALE_SPEED_FINE = 0.05f;  // alt+scale: slow creep

    // lighting
    inline constexpr float LIGHT_DIR_X= 1.0f;
    inline constexpr float LIGHT_DIR_Y= 2.0f;
    inline constexpr float LIGHT_DIR_Z= 1.0f;
    inline constexpr float LIGHT_AMBIENT= 0.55f;
    inline constexpr float LIGHT_DIFF= 0.85f;

    // shadow
    inline constexpr int SHADOW_MAP_SIZE = 4096;   // depth tex resolution, higher = sharper shadows
    inline constexpr float SHADOW_ORTHO_SIZE = 120.0f; // world units covered by shadow frustum
    inline constexpr float SHADOW_NEAR = 1.0f;
    inline constexpr float SHADOW_FAR  = 300.0f;
    inline constexpr float SHADOW_BIAS = 0.0015f;    

    // ground & terrain
    inline constexpr float GROUND_HALF_EXTENT= 200.0f;
    inline constexpr float GROUND_Y_OFFSET= -0.002f;
    inline constexpr float GROUND_KD= 0.18f;   // uniform RGB, dark asphalt

    // heightfield terrain
    inline constexpr int TERRAIN_ROWS = 100; // grid cells z
    inline constexpr int TERRAIN_COLS = 100; // grid cells x
    inline constexpr float TERRAIN_CELL_SIZE = 8.0f; // meters per cell
    inline constexpr float TERRAIN_MAX_Y = 40.0f;  // sculpt clamp ceiling
    inline constexpr float TERRAIN_MIN_Y = -10.0f;  // sculpt clamp floor for below sea level beach areas

    // surface type names index matches SurfaceType enum
    inline constexpr const char* SURFACE_NAMES[7] = {
        "none", "asphalt", "gravel", "dirt", "sand", "grass", "cement"
    };

    // terrain sculpt brush in editor
    // everything meters
    inline constexpr float TERRAIN_BRUSH_RADIUS_DEFAULT = 8.0f;
    inline constexpr float TERRAIN_BRUSH_RADIUS_MIN = 2.0f;
    inline constexpr float TERRAIN_BRUSH_RADIUS_MAX = 40.0f;
    inline constexpr float TERRAIN_BRUSH_STRENGTH = 0.12;
    inline constexpr float TERRAIN_BRUSH_SMOOTH_STRENGTH = 0.35f;

    // slope physics
    inline constexpr float TERRAIN_SLOPE_GRAVITY_SCALE = 1.0f; // multiplier on gravity component along slopes
    inline constexpr float TERRAIN_SNAP_LERP_SPEED = 12.0f; // how fast trike y snaps height

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

    // street lamps, sign posts, fences
    inline constexpr float DYN_POLE_MASS = 18.0f;
    inline constexpr float DYN_POLE_RESTITUTION = 0.25f;
    inline constexpr float DYN_POLE_FRICTION = 0.90f;

    // food stalls
    // fruit stands, palengke stalls
    inline constexpr float DYN_STALL_MASS = 25.0f;
    inline constexpr float DYN_STALL_RESTITUTION = 0.15f;
    inline constexpr float DYN_STALL_FRICTION = 0.70f;

    // light chairs
    inline constexpr float DYN_CHAIR_MASS = 4.5f;
    inline constexpr float DYN_CHAIR_RESTITUTION = 0.25f;
    inline constexpr float DYN_CHAIR_FRICTION = 0.75f;

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

    // bus
    inline constexpr float DYN_BUS_MASS = 6000.0f;
    inline constexpr float DYN_BUS_RESTITUTION = 0.08f;
    inline constexpr float DYN_BUS_FRICTION = 0.95f;

    inline constexpr float DYN_TRUCK_MASS = 8000.0f;
    inline constexpr float DYN_TRUCK_RESTITUTION = 0.08f;
    inline constexpr float DYN_TRUCK_FRICTION = 0.95f;

    // individual fruits
    inline constexpr float DYN_FRUIT_MASS = 0.3f;
    inline constexpr float DYN_FRUIT_RESTITUTION = 0.70f;
    inline constexpr float DYN_FRUIT_FRICTION = 0.60f;

    // basketball
    inline constexpr float DYN_BBALL_MASS = 0.62f;
    inline constexpr float DYN_BBALL_RESTITUTION = 0.85f;
    inline constexpr float DYN_BBALL_FRICTION = 0.60f;

    // fallback
    inline constexpr float DYN_DEFAULT_MASS = 8.0f;
    inline constexpr float DYN_DEFAULT_RESTITUTION = 0.55f;
    inline constexpr float DYN_DEFAULT_FRICTION = 0.75f;

    // ocean
    inline constexpr float OCEAN_Y_LEVEL = -0.4f; // world Y of water surface
    inline constexpr float OCEAN_GRID_SPACING = 2.0f; // metres between wave verts
    inline constexpr float OCEAN_WAVE_AMP = 0.22f; // metres, peak displacement
    inline constexpr float OCEAN_WAVE_FREQ = 0.18f; // spatial frequency
    inline constexpr float OCEAN_WAVE_SPEED = 0.9f; // time multiplier
    inline constexpr float OCEAN_WAVE_AMP2 = 0.12f; // second wave layer
    inline constexpr float OCEAN_WAVE_FREQ2 = 0.27f; // second layer spatial freq
    inline constexpr float OCEAN_WAVE_SPEED2 = 1.3f; // second layer time mult
    inline constexpr float OCEAN_SHALLOW_DIST = 12.0f; // metres from shore edge for teal tint

}