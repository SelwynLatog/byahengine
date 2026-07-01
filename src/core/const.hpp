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

    inline constexpr const char* SKY_IMAGE_PATH = "../assets/props/sky_day.jpg";

    // day cycle
    // 10 real minutes = 1 full in-game day
    inline constexpr float DAY_DURATION_SECONDS = 600.0f;

    // configure in 24h clock format to select which time specifically
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
    inline constexpr float LIGHT_NIGHT_R = 0.30f;
    inline constexpr float LIGHT_NIGHT_G = 0.35f;
    inline constexpr float LIGHT_NIGHT_B = 0.55f;
    inline constexpr float LIGHT_NIGHT_AMBIENT = 0.07f;
    inline constexpr float LIGHT_NIGHT_DIFF = 1.0f;

    // sound stuff
    // max concurrent one-shot voices per pool
    inline constexpr int AUDIO_IMPACT_VOICES  = 8;
    inline constexpr int AUDIO_VOICE_VOICES = 4;
    inline constexpr int AUDIO_STEP_VOICES = 4;

    // ambience
    inline constexpr int MAX_AMBIENCE_ZONES = 64; // max placed zones in world
    inline constexpr int MAX_AMBIENCE_SLOTS = 8; // max concurrently audible zones
    inline constexpr float AMBIENCE_RADIUS_DEFAULT = 20.0f;
    inline constexpr float AMBIENCE_RADIUS_MIN = 5.0f;
    inline constexpr float AMBIENCE_RADIUS_MAX = 120.0f;
    inline constexpr float AMBIENCE_RADIUS_STEP = 2.0f; // [ / ] key increment
    inline constexpr float FADE_SPEED = 1.2f; // vol units/sec fade in+out
    inline constexpr float RAIN_VOL_MAX = 0.85f;
    inline constexpr float NIGHT_THRESH = 0.35f; // night_factor above this activates AMBIENCE_NIGHT zones


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

    inline constexpr float TRIKE_LATERAL_FRICTION= 22.0f; // resists sideways slip N/(m/s)
    inline constexpr float TRIKE_ROLL_STIFFNESS= 40.0f; // suspension spring restoring roll
    inline constexpr float TRIKE_ROLL_DAMPING= 20.0f; // damping on roll oscillation
    inline constexpr float TRIKE_ROLLOVER_THRESHOLD= 38.0f; // degrees, tip point
    inline constexpr float TRIKE_RESPAWN_DELAY= 2.5f; // seconds before reset after tip

    // suspension
    inline constexpr float TRIKE_SUSP_REST = 0.0f; // ride height above ground_y (wheel radius already baked into model offset)
    inline constexpr float TRIKE_SUSP_STIFFNESS = 18000.0f; // N/m soft enough to bounce but stiff enough not to wallow
    inline constexpr float TRIKE_SUSP_DAMPING = 900.0f; // N·s/m critically damped feel
    inline constexpr float TRIKE_SUSP_BUMP = 0.12f; // max compression travel, meters
    inline constexpr float TRIKE_SUSP_DROOP = 0.08f; // max droop travel, meters


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
    inline constexpr const char* TRIKE_MODEL_PATH = "../assets/props/TRAYSIKEL.obj";
    inline constexpr const char* TRIKE_PARTS_MODEL_PATH = "../assets/props/TRAYSIKEL_parts.obj";
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

    // trike headlights
    // why is it const position tuning instead of per mesh part separation?
    // becayse that would mean heaving another dedicated trike_headlight finder
    // the chain is translate(render_pos) * rotate(heading) * rotate(roll) * rotate(pitch) 
    //* rotate(yaw_offset) * rotate(shake) * translate(-sc) * scale(model_scale)
    // that means I have to duplicate or expose math outside trike_model.cpp
    // just to get one position. 
    // That's coupling the light system to the render transform internals
    // messy & fragile every time the model matrix changes
    // Simplicity over fragile "optimal" methods
    inline constexpr float HEADLIGHT_OFFSET_FWD = 2.2f;  // metres forward from rear axle
    inline constexpr float HEADLIGHT_OFFSET_Y = 0.85f; // metres above trike position
    inline constexpr float HEADLIGHT_RADIUS = 35.0f; // falloff distance
    inline constexpr float HEADLIGHT_INTENSITY = 4.0f;  // peak brightness
    inline constexpr float HEADLIGHT_R = 1.00f; // warm white
    inline constexpr float HEADLIGHT_G = 0.95f;
    inline constexpr float HEADLIGHT_B = 0.80f;
    inline constexpr float HEADLIGHT_NIGHT_THRESH  = 0.15f; // night_factor above this activates headlights
    inline constexpr float HEADLIGHT_CONE_DEG = 24.0f; // half-angle of beam cone
    inline constexpr float HEADLIGHT_CONE_SOFT_DEG = 18.0f; // inner edge full brightness inside this


    // point lights
    inline constexpr int MAX_POINT_LIGHTS = 150;
    inline constexpr float LIGHT_DEFAULT_RADIUS = 20.0f;
    inline constexpr float LIGHT_DEFAULT_INTENSITY = 4.5f;

    // light cull
    inline constexpr float LIGHT_CULL_DIST = 120.0f; // metres, lights beyond this aren't uploaded
    inline constexpr float LIGHT_CULL_DIST_SQ = LIGHT_CULL_DIST * LIGHT_CULL_DIST;

    inline constexpr float PROP_CULL_DIST = 200.0f;
    inline constexpr float PROP_CULL_DIST_SQ = PROP_CULL_DIST * PROP_CULL_DIST;

    inline constexpr float NPC_CULL_DIST = 80.0f;
    inline constexpr float NPC_CULL_DIST_SQ = NPC_CULL_DIST * NPC_CULL_DIST;

    // streetlight warm yellow
    inline constexpr float LIGHT_DEFAULT_R = 1.00f;
    inline constexpr float LIGHT_DEFAULT_G = 0.85f;
    inline constexpr float LIGHT_DEFAULT_B = 0.50f;

    // shadow
    inline constexpr int SHADOW_MAP_SIZE = 2048;   // depth tex resolution, higher = sharper shadows
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

    // traffic cones
    inline constexpr float DYN_CONE_MASS = 3.0f;
    inline constexpr float DYN_CONE_RESTITUTION = 0.60f;
    inline constexpr float DYN_CONE_FRICTION = 0.70f;

    // garbage bins
    inline constexpr float DYN_BIN_MASS = 12.0f;
    inline constexpr float DYN_BIN_RESTITUTION = 0.45f;
    inline constexpr float DYN_BIN_FRICTION = 0.70f;

    // garbage bags
    inline constexpr float DYN_BAG_MASS = 1.5f;
    inline constexpr float DYN_BAG_RESTITUTION = 0.20f;
    inline constexpr float DYN_BAG_FRICTION = 0.95f;

    // parked motorcycles
    inline constexpr float DYN_MOTORCYCLE_MASS = 120.0f;
    inline constexpr float DYN_MOTORCYCLE_RESTITUTION = 0.20f;
    inline constexpr float DYN_MOTORCYCLE_FRICTION = 0.85f;

    // parked trikes
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
    inline constexpr float OCEAN_GRID_SPACING = 0.35f; // metres between wave verts
    inline constexpr float OCEAN_WAVE_AMP = 0.12f; // metres, peak displacement
    inline constexpr float OCEAN_WAVE_FREQ = 0.18f; // spatial frequency
    inline constexpr float OCEAN_WAVE_SPEED = 0.9f; // time multiplier
    inline constexpr float OCEAN_WAVE_AMP2 = 0.06f; // second wave layer
    inline constexpr float OCEAN_WAVE_FREQ2 = 0.27f; // second layer spatial freq
    inline constexpr float OCEAN_WAVE_SPEED2 = 1.3f; // second layer time mult
    inline constexpr float OCEAN_SHALLOW_DIST = 12.0f; // metres from shore edge for teal tint

    // rain
    inline constexpr bool RAIN_FORCE_ENABLE = false; // enable to insta trigger rain at build, debug
    inline constexpr bool RAIN_PAUSE = false; // enable to pause rain for debugging streak texture
    inline constexpr int RAIN_PARTICLE_COUNT = 8000;
    inline constexpr float RAIN_FALL_SPEED = 22.0f;
    inline constexpr float RAIN_STREAK_SPEED = 0.55f;
    inline constexpr float RAIN_STREAK_LENGTH = 0.55f; // quad height
    inline constexpr float RAIN_STREAK_WIDTH = 0.01f; // quad width
    inline constexpr float RAIN_BOX_HALF_XZ = 55.0f; // spawn box rad around cam XZ
    inline constexpr float RAIN_BOX_HEIGHT = 22.0f;  // spawn height above cam
    inline constexpr float RAIN_WIND_X = 1.4f; // m/s wind drift X
    inline constexpr float RAIN_WIND_Z = 0.5f; // m/s wind drift Z
    inline constexpr float RAIN_ALPHA = 0.15f; // streak base opacity
    // random trigger: rain starts every RAIN_INTERVAL_MIN..MAX seconds, lasts RAIN_DUR seconds
    inline constexpr float RAIN_INTERVAL_MIN = 30.0f;
    inline constexpr float RAIN_INTERVAL_MAX = 90.0f;
    inline constexpr float RAIN_DUR = 600.0f; // sec
    inline constexpr float RAIN_START_CHANCE = 0.2f;

    // thunder + lightning
    // RAIN_THUNDERSTORM enables everything, set true to debug
    inline constexpr bool RAIN_THUNDERSTORM = false;
    inline constexpr float THUNDER_FLASH_ALPHA_MAX = 0.75f; // peal white overlay flash
    inline constexpr float THUNDER_FLASH_DECAY = 6.0f; // how fast flash fades per sec
    inline constexpr float THUNDER_PREFLASH_ALPHA = 0.28f; // dim pre-flash
    inline constexpr float THUNDER_PREFLASH_DECAY = 9.0f; // pre-flash dies fast
    inline constexpr float THUNDER_PREFLASH_GAP = 0.08f; // seconds between pre and main
    inline constexpr float THUNDER_DOUBLE_CHANCE = 0.4f; //% of strikes get a pre-flash
    inline constexpr float THUNDER_INTERVAL_MIN = 18.0f; // secs between strikes (min)
    inline constexpr float THUNDER_INTERVAL_MAX = 45.0f; // secs between strikes (max)
    inline constexpr float THUNDER_AUDIO_DELAY_MIN = 0.0f; // delay between flash and boom (sec)
    inline constexpr float THUNDER_AUDIO_DELAY_MAX = 1.8f; // simulates distance


    // rain splashes
    inline constexpr int RAIN_SPLASH_MAX = 1200;
    inline constexpr float RAIN_SPLASH_LIFE = 0.55f; // higher = more visible particles
    inline constexpr float RAIN_SPLASH_RADIUS = 0.35f; // higher = bigger splash rad


    // driver / NPC character scale
    // scale against actual height axis, not longest axis like trike
    inline constexpr float DRIVER_TARGET_HEIGHT = 1.55f; // metres
    inline constexpr float DRIVER_SEAT_OFFSET_X = 0.65f; // forward/back in trike local space
    inline constexpr float DRIVER_SEAT_OFFSET_Y = 0.01f; // seat height above trike position
    inline constexpr float DRIVER_SEAT_OFFSET_Z = -0.3f;  // lateral

    inline constexpr float NPC_WALK_SPEED = 1.4f;
    inline constexpr float NPC_ARRIVE_DIST = 0.6f;
    inline constexpr float NPC_RAGDOLL_DURATION = 3.5f;
    inline constexpr float NPC_GRAVITY = 9.81f;
    inline constexpr float NPC_ANG_DRAG = 0.85f;
    inline constexpr float NPC_LIN_DRAG = 0.75f;
    inline constexpr float NPC_HAIL_RANGE_SQ = 36.0f; // 6m radius, checked in app
    inline constexpr float NPC_HAIL_TIMER_MIN = 8.0f;  // seconds between hail attempts
    inline constexpr float NPC_HAIL_TIMER_MAX = 20.0f;

    // fare system
    inline constexpr float FARE_RATE_PER_METRE = 0.00001; // pesos per metre <- adjust based on horrible gas prices in 2026
    inline constexpr float FARE_BASE = 10.0f; // base fare on pickup
    inline constexpr float DROPOFF_SLOW_THRESHOLD = 120.0f; // seconds, 2 minutes

}