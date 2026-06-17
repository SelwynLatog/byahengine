#pragma once
#include "../world/world_object.hpp"
#include "../world/height_field.hpp"
#include "../world/ambience_zone.hpp"
#include "../core/const.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

// which transform operation currently in use
enum EditorTool{
    TOOL_TRANSLATE,
    TOOL_ROTATE,
    TOOL_SCALE
};

// which editor sub mode is active
enum EditorMode{
    MODE_OBJECT, // default - place/select/move props
    MODE_TERRAIN, // sculpt heightfield
    MODE_ROAD, // place/edit road spline control points
    MODE_OCEAN, // place/resize ocean zones
    MODE_LIGHT, // place light source, adjust intensity
    MODE_POSE,
    MODE_AUDIO,
    MODE_AMBIENCE
};

// editor mode state lives in App
// reset when toggling back to drive mode
struct EditorState{
    bool active = false; // true = editor, false = drive

    // freecam
    glm::vec3 cam_pos = glm::vec3(0.0f, 8.0f, 0.0f);
    float cam_yaw = 0.0f;
    float cam_pitch = 0.0f; // radians clamped to avoid gimbal flip

    std::string selected_model = ""; // filename.obj
    int selected_id = -1;
    glm::vec3 ghost_pos = glm::vec3(0.0f); // pos where next obj will land
    bool placement_valid = false; // ghost snapped to ground

    // active tool
    EditorTool tool = TOOL_TRANSLATE;

    // prop palette scans assets/ root only
    std::vector<std::string> prop_list;
    int prop_page = 0;

    // entity palette scans assets/entity/ for Driver/NPC models
    // active when selected object is PEDESTRIAN
    std::vector<std::string> entity_list;
    int entity_page = 0;

    // dynamic behavior selector
    // when DYNAMIC, select obj type N key cycles through DYN_PRESETS
    int dyn_preset_index = 0;

    // copy/paste clipboard
    // has_clipboard = false until user copies something
    bool has_clipboard = false;
    WorldObject clipboard; // last copied object, position ignored on paste

    // editor mode
    EditorMode mode = MODE_OBJECT;

    float brush_radius = Const::TERRAIN_BRUSH_RADIUS_DEFAULT;
    bool brush_smooth = false;

    // paint sub-mode inside MODE_TERRAIN
    bool paint_mode = false;
    SurfaceType paint_surface = SURFACE_GRASS;

    int active_road_id = -1;
    int selected_point_idx = -1;
    bool road_placing = false;
    bool show_hitboxes = false;

    // light editor
    int selected_light_id = -1;
    // staging values for the selected light, edited live before confirm
    glm::vec3 light_edit_color = glm::vec3(Const::LIGHT_DEFAULT_R, Const::LIGHT_DEFAULT_G, Const::LIGHT_DEFAULT_B);
    float light_edit_radius = Const::LIGHT_DEFAULT_RADIUS;
    float light_edit_intensity = Const::LIGHT_DEFAULT_INTENSITY;

    // pose editor
    int pose_bone = 0;
    // incremental quaternion per bone, applied on top of pose_sit()
    // stored as quat so rotations accumulate without gimbal lock
    glm::quat pose_quat[6] = {
        glm::quat(1,0,0,0), glm::quat(1,0,0,0), glm::quat(1,0,0,0),
        glm::quat(1,0,0,0), glm::quat(1,0,0,0), glm::quat(1,0,0,0)
    };
    glm::vec3 pose_seat = glm::vec3(
        Const::DRIVER_SEAT_OFFSET_X,
        Const::DRIVER_SEAT_OFFSET_Y,
        Const::DRIVER_SEAT_OFFSET_Z);
    glm::vec3 pose_offset[6] = {};       // per bone translation in model space
    bool pose_numpad_translate = false;
    int pose_npc_id = -1; // -1 = driver, else npc world object id
    bool pose_editing_hail = false; // true = editing hail pose, false = mount


    // audio editor
    // active when MODE_AUDIO, requires selected_id != -1
    int audio_slot = 0; // which slot is being edited, maps to AudioSlot enum
    int audio_file_page = 0; // scroll offset in audio file list
    std::vector<std::string> audio_file_list; // scanned from assets/audio/

    // ambience editor
    // active when MODE_AMBIENCE
    int selected_zone_id = -1; // -1 = none selected
    int ambience_file_page = 0; // scroll offset in audio file list
    bool ambience_placing = false; // true = ghost zone follows ground cursor, LMB confirms

};