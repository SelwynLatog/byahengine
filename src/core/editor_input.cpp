#include "editor_input.hpp"
#include "const.hpp"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../renderer/editor_renderer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <fstream>
#include "../world/npc.hpp"
#include "../world/ambience_zone.hpp"

/*
=============================================================
  EDITOR CONTROLS
=============================================================

  [TAB]           toggle editor / drive mode

  --- OBJECT MODE (default) ---
  [L CLICK]       select object / place new prop
  [CTRL+L CLICK]  select smallest object under cursor
  [SHIFT+L CLICK] place on top of selected object
  [DEL]           delete selected object
  [B]             cycle behavior: STATIC > DYNAMIC > DECORATION > PEDESTRIAN
  [N]             cycle physics preset on DYNAMIC object
  [T]             translate tool
  [R]             rotate tool
  [Y]             scale tool
  [ARROW KEYS]    move / rotate / scale selected object
  [SHIFT+ARROWS]  fine move (5cm steps)
  [PgUp / PgDn]   nudge Y up / down
  [SHIFT+PgUp / PgDn] fine nudge
  [1-9]           select prop from current page
  [[ / ]]         prev / next prop page
  [CTRL+C]        copy selected object
  [CTRL+V]        paste at cursor
  [CTRL+S]        save map
  [F5]            rescan assets/

  --- TERRAIN MODE [H] ---
  [L CLICK hold]        raise terrain
  [R CLICK hold]        lower terrain
  [SHIFT+L/R CLICK]     smooth brush
  [[ / ]]               shrink / grow brush radius
  [CTRL+Z]              undo last sculpt stroke (32 levels)
  [CTRL+SHIFT+F]        flatten entire terrain to y=0 (undoable)
  [CTRL+S]              save
  [H]                   exit terrain mode

  --- ROAD MODE [M] ---
  [L CLICK]       add control point to active spline
  [R CLICK]       undo last control point
  [[ / ]]         cycle road type (asphalt>gravel>dirt>sand>grass>cement)
  [PgUp / PgDn]   nudge selected point Y up / down
  [SHIFT+PgUp/Dn] fine Y nudge (5cm)
  [ENTER]         finish spline, return to object mode
  [DEL]           delete entire active spline
  [CTRL+S]        save
  [CTRL+SHIFT+W]  wipes out entire canvas to blank 
  [M]             exit road mode

 --- POSE MODE [K] ---
 [F]             cycle active bone: TORSO > HEAD > LEG_L > LEG_R > ARM_L > ARM_R
 [ARROWS]        rotate active bone X/Y axis
 [PgUp / PgDn]   rotate active bone Z axis
 [SHIFT+any]     fine mode (10x slower)
 [NP0]           toggle numpad between SEAT mode and BONE TRANSLATE mode
  SEAT mode:
    [NP8/2]     move seat Z (fwd/back)
    [NP4/6]     move seat X (left/right)
    [NP+/-]     move seat Y (up/down)
  BONE TRANSLATE mode: same keys, moves active bone mesh offset
 [ENTER]         dump current pose as code (paste into driver_anim.cpp)
 [LMB]           click pedestrian to switch pose target
 [Ctrl+H]        save current pose as HAIL pose on selected NPC
 [Ctrl+M]        save current pose as MOUNT pose on selected NPC
 [Ctrl+S]        save driver pose to driver_pose.txt (driver only)
 [K]             exit pose mode

=============================================================
*/

// key state tracking to prevent held key repeat
static bool s_tab_last = false;
static bool s_del_last = false;
static bool s_lmb_last = false;
static bool s_b_last = false;
static bool s_n_last = false;
static bool s_c_last = false;
static bool s_v_last = false;
static bool s_f5_last = false;
static bool s_h_last = false; // terrain mode toggle
static bool s_m_last = false; // road mode toggle
static bool s_rmb_last = false; // road spline: right click cancels, terrain: lower
static bool s_enter_last = false; // road spline : finish curr spline

// dynamic physics preset table
// adding a new type = one new row here + a const in const.hpp
// N key cycles through this when selected object is DYNAMIC
// much easier for me to scale than slopping down on else statements
struct DynPreset {
    const char* name;
    float mass;
    float restitution;
    float friction;
};

static const DynPreset DYN_PRESETS[] = {
    { "CONE",       Const::DYN_CONE_MASS,          Const::DYN_CONE_RESTITUTION,         Const::DYN_CONE_FRICTION         },
    { "BIN",        Const::DYN_BIN_MASS,           Const::DYN_BIN_RESTITUTION,          Const::DYN_BIN_FRICTION          },
    { "BAG",        Const::DYN_BAG_MASS,           Const::DYN_BAG_RESTITUTION,          Const::DYN_BAG_FRICTION          },
    { "CART",       Const::DYN_CART_MASS,          Const::DYN_CART_RESTITUTION,         Const::DYN_CART_FRICTION         },
    { "MOTORCYCLE", Const::DYN_MOTORCYCLE_MASS,    Const::DYN_MOTORCYCLE_RESTITUTION,   Const::DYN_MOTORCYCLE_FRICTION   },
    { "POLE",       Const::DYN_POLE_MASS,          Const::DYN_POLE_RESTITUTION,         Const::DYN_POLE_FRICTION         },
    { "TRIKE",      Const::DYN_TRIKE_MASS,         Const::DYN_TRIKE_RESTITUTION,        Const::DYN_TRIKE_FRICTION        },
    { "RAILING",    Const::DYN_RAILING_MASS,       Const::DYN_RAILING_RESTITUTION,      Const::DYN_RAILING_FRICTION      },
    { "STALL",      Const::DYN_STALL_MASS,         Const::DYN_STALL_RESTITUTION,        Const::DYN_STALL_FRICTION        },
    { "BARREL",     Const::DYN_BARREL_MASS,        Const::DYN_BARREL_RESTITUTION,       Const::DYN_BARREL_FRICTION       },
    { "CAR",        Const::DYN_CAR_MASS,           Const::DYN_CAR_RESTITUTION,          Const::DYN_CAR_FRICTION          },
    { "TRUCK",      Const::DYN_TRUCK_MASS,         Const::DYN_TRUCK_RESTITUTION,        Const::DYN_TRUCK_FRICTION        },
    { "FRUIT",      Const::DYN_FRUIT_MASS,         Const::DYN_FRUIT_RESTITUTION,        Const::DYN_FRUIT_FRICTION        },
    { "BUS",        Const::DYN_BUS_MASS,           Const::DYN_BUS_RESTITUTION,          Const::DYN_BUS_FRICTION          },
    { "CHAIR",      Const::DYN_CHAIR_MASS,         Const::DYN_CHAIR_RESTITUTION,        Const::DYN_CHAIR_FRICTION        },
    { "BASKETBALL", Const::DYN_BBALL_MASS,         Const::DYN_BBALL_RESTITUTION,        Const::DYN_BBALL_FRICTION        },
    { "DEFAULT",    Const::DYN_DEFAULT_MASS,       Const::DYN_DEFAULT_RESTITUTION,      Const::DYN_DEFAULT_FRICTION      },
};
static const int DYN_PRESET_COUNT = (int)(sizeof(DYN_PRESETS) / sizeof(DYN_PRESETS[0]));

// translate tool arrow key edge triggers
static bool s_arr_left_last = false;
static bool s_arr_right_last = false;
static bool s_arr_up_last = false;
static bool s_arr_down_last = false;
static bool s_pgup_last = false;
static bool s_pgdn_last = false;

void editor_scan_props(EditorState& editor, const char* assets_dir){
    editor.prop_list.clear();

    // scan through assets/ and collect every .obj filename
    // store filename but reconstruct fullpath at placement time
    for (const auto& entry : std::filesystem::directory_iterator(assets_dir)){
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".obj") continue;
        editor.prop_list.push_back(entry.path().filename().string());
    }

    // sort alphabetically so the palette is stable across runs
    std::sort(editor.prop_list.begin(), editor.prop_list.end());

    std::cout << "[editor] found " << editor.prop_list.size() << " props in " << assets_dir << "\n";

    // scan entity subfolder separately for NPC models
    editor.entity_list.clear();
    std::string entity_dir = std::string(assets_dir) + "/entity";
    if (std::filesystem::exists(entity_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(entity_dir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".obj") continue;
            editor.entity_list.push_back(entry.path().filename().string());
        }
        std::sort(editor.entity_list.begin(), editor.entity_list.end());
    }   
    // also merge entity list into prop list with entity/ prefix
    // so they appear in the normal object mode palette
    for (const auto& e : editor.entity_list)
        editor.prop_list.push_back("entity/" + e);

    std::cout << "[editor] found " << editor.entity_list.size() << " entities in " << entity_dir << "\n";
}

void editor_scan_audio(EditorState& editor, const char* assets_dir){
    editor.audio_file_list.clear();
    std::string audio_root = std::string(assets_dir) + "/audio";
    if (!std::filesystem::exists(audio_root)) return;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(audio_root)){
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension();
        if (ext != ".wav" && ext != ".ogg") continue;
        // store relative to assets/audio/ so paths are short
        auto rel = std::filesystem::relative(entry.path(),
            std::string(assets_dir)).string();
        // normalize separators
        std::replace(rel.begin(), rel.end(), '\\', '/');
        editor.audio_file_list.push_back(rel);
    }
    std::sort(editor.audio_file_list.begin(), editor.audio_file_list.end());
    std::cout << "[editor] found " << editor.audio_file_list.size() << " audio files\n";
}

bool editor_raycast_ground( double mx, double my,
    const glm::mat4& view, const glm::mat4& proj,
    int screen_w, int screen_h, glm::vec3& out_pos,
    const HeightField* terrain){

    // convert screen pixel to NDC [-1, 1]
    float ndc_x = (2.0f * (float)mx / screen_w) - 1.0f;
    float ndc_y = 1.0f - (2.0f * (float)my / screen_h);

    // unproject two points to NDC z=-1 and z=1 to get ray direction
    glm::mat4 inv = glm::inverse(proj * view);

    glm::vec4 near_pt = inv * glm::vec4(ndc_x, ndc_y, -1.0f, 1.0f);
    glm::vec4 far_pt = inv * glm::vec4(ndc_x, ndc_y,  1.0f, 1.0f);

    near_pt /= near_pt.w;
    far_pt /= far_pt.w;

    glm::vec3 ray_origin = glm::vec3(near_pt);
    glm::vec3 ray_dir = glm::normalize(glm::vec3(far_pt) - ray_origin);

    if (terrain && terrain->rows > 0){
        // iterative ray march against heightfield
        // step along ray until we dip below terrain surface
        // then binary search the crossing for precision
        float step = terrain->cell_size * 0.5f;
        float t = 0.0f;
        float t_max = Const::CAM_FAR;
        float prev_t = 0.0f;

        while (t < t_max){
            glm::vec3 p = ray_origin + ray_dir * t;
            float ground = heightfield_sample(*terrain, p.x, p.z);
            if (p.y <= ground){
                float lo =  prev_t, hi = t;
                for (int i = 0; i<8; i++){
                    float mid = (lo + hi) * 0.5f;
                    glm::vec3 mp = ray_origin + ray_dir * mid;
                    if (mp.y <= heightfield_sample(*terrain, mp.x, mp.z)) hi = mid;
                    else lo = mid;
                }
                out_pos = ray_origin + ray_dir * ((lo+hi) * 0.5f);
                out_pos.y = heightfield_sample(*terrain, out_pos.x, out_pos.z);
                return true;
            }
            prev_t = t;
            t += step;
        }
        return false;
    }

    // flat y=0 fallback when no terrain
    if (std::abs(ray_dir.y) < 1e-6f) return false;
    float t = -ray_origin.y / ray_dir.y;
    if (t < 0.0f) return false;
    out_pos = ray_origin + ray_dir * t;
    return true;

}

int editor_raycast_objects(double mx, double my, const glm::mat4& view, const glm::mat4& proj,
    int screen_w, int screen_h, const WorldMap& map, const EditorRenderer& er,
    bool prefer_small){

    float ndc_x = (2.0f * (float)mx / screen_w) - 1.0f;
    float ndc_y = 1.0f - (2.0f * (float)my / screen_h);

    glm::mat4 inv = glm::inverse(proj * view);
    glm::vec4 near_pt = inv * glm::vec4(ndc_x, ndc_y, -1.0f, 1.0f);
    glm::vec4 far_pt  = inv * glm::vec4(ndc_x, ndc_y,  1.0f, 1.0f);
    near_pt /= near_pt.w;
    far_pt  /= far_pt.w;

    glm::vec3 ray_origin = glm::vec3(near_pt);
    glm::vec3 ray_dir = glm::normalize(glm::vec3(far_pt) - ray_origin);

    float closest_t = 1e9f;
    float smallest_vol = 1e9f;
    int hit_id = -1;

    for (const auto& o : map.objects){
        glm::vec3 aabb_min, aabb_max;
        auto bit = er.prop_bounds.find(o.model_path);
        if (bit != er.prop_bounds.end()){
            float yoff = er.prop_y_offset.count(o.model_path)
                ? er.prop_y_offset.at(o.model_path) : 0.0f;
            
            float yaw = o.rotation.y;
            glm::vec3 lmin = bit->second.local_min;
            glm::vec3 lmax = bit->second.local_max;
            glm::vec3 smin = { lmin.x * o.scale.x, (lmin.y + yoff) * o.scale.y, lmin.z * o.scale.z };
            glm::vec3 smax = { lmax.x * o.scale.x, (lmax.y + yoff) * o.scale.y, lmax.z * o.scale.z };
            float c = std::cos(yaw), s = std::sin(yaw);
            aabb_min = glm::vec3( 1e9f);
            aabb_max = glm::vec3(-1e9f);
            for (int k = 0; k < 8; k++){
                glm::vec3 corner = {
                    (k & 1) ? smax.x : smin.x,
                    (k & 2) ? smax.y : smin.y,
                    (k & 4) ? smax.z : smin.z,
                };
                glm::vec3 world = o.position + glm::vec3(
                    c * corner.x - s * corner.z, corner.y, s * corner.x + c * corner.z);
                aabb_min = glm::min(aabb_min, world);
                aabb_max = glm::max(aabb_max, world);
            }
        }
        else {
            glm::vec3 half = o.scale * 0.5f;
            aabb_min = o.position + glm::vec3(-half.x, 0.0f, -half.z);
            aabb_max = o.position + glm::vec3( half.x, o.scale.y, half.z);
        }

        float tmin = 0.0f, tmax = 1e9f;
        for (int i = 0; i < 3; i++){
            float inv_d = 1.0f / ray_dir[i];
            float t0 = (aabb_min[i] - ray_origin[i]) * inv_d;
            float t1 = (aabb_max[i] - ray_origin[i]) * inv_d;
            if (inv_d < 0.0f) std::swap(t0, t1);
            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);
            if (tmax < tmin) goto next_object;
        }

        if (prefer_small){
            glm::vec3 sz  = aabb_max - aabb_min;
            float vol = sz.x * sz.y * sz.z;
            if (vol < smallest_vol){ smallest_vol = vol; hit_id = o.id; }
        }
        else {
            if (tmin < closest_t){ closest_t = tmin; hit_id = o.id; }
        }

        next_object:;
    }

    return hit_id;
}


void editor_input_update(EditorState& editor, WorldMap& map, EditorRenderer& er,
    GLFWwindow* window, const glm::mat4& view, const glm::mat4& proj,
    int screen_w, int screen_h, float dt, bool& map_dirty){

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);

    // update ghost pos every frame by raycasting mouse to ground
    editor.placement_valid = editor_raycast_ground(
        mx, my, view, proj, screen_w, screen_h, editor.ghost_pos,
        (map.terrain.rows > 0) ? &map.terrain : nullptr);
    
    // snap ghost to grid
    if (editor.placement_valid){
        editor.ghost_pos.x = std::round(editor.ghost_pos.x / Const::EDITOR_GRID_SNAP) * Const::EDITOR_GRID_SNAP;
        editor.ghost_pos.z = std::round(editor.ghost_pos.z / Const::EDITOR_GRID_SNAP) * Const::EDITOR_GRID_SNAP;
        editor.ghost_pos.y = heightfield_sample(map.terrain, editor.ghost_pos.x, editor.ghost_pos.z);
    }

    // P toggles paint sub-mode inside terrain mode
    static bool s_p_last = false;
    bool p_down = glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS;
    if (p_down && !s_p_last && editor.mode == MODE_TERRAIN){
        editor.paint_mode = !editor.paint_mode;
        er.terrain_surface_dirty = true;
        std::cout << "[editor] paint mode " << (editor.paint_mode ? "ON" : "OFF") << "\n";
    }
    s_p_last = p_down;

    // H to toggle terain sculpt mode
    bool h_down = glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS;
    bool ctrl_h = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
    if (h_down && !s_h_last && !ctrl_h){
        editor.mode = (editor.mode == MODE_TERRAIN) ? MODE_OBJECT : MODE_TERRAIN;
        er.terrain_surface_dirty = true;
        std::cout << "[editor] mode -> " << (editor.mode == MODE_TERRAIN ? "TERRAIN" : "OBJECT") << "\n";
    }
    s_h_last = h_down;

    // M to toggle to raod spline mode
    bool m_down = glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS;
    bool ctrl_m = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
    if (m_down && !s_m_last && !ctrl_m){
        editor.mode = (editor.mode == MODE_ROAD) ? MODE_OBJECT : MODE_ROAD;
        editor.road_placing = (editor.mode == MODE_ROAD);
        std::cout << "[editor] mode -> " << (editor.mode == MODE_ROAD ? "ROAD" : "OBJECT") << "\n";
    }
    s_m_last = m_down;

    // O to toggle ocean mode
    static bool s_o_last = false;
    bool o_down = glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS;
    if (o_down && !s_o_last){
        editor.mode = (editor.mode == MODE_OCEAN) ? MODE_OBJECT : MODE_OCEAN;
        std::cout << "[editor] mode -> " << (editor.mode == MODE_OCEAN ? "OCEAN" : "OBJECT") << "\n";
    }
    s_o_last = o_down;

    // L to toggle light placement mode
    static bool s_l_last = false;
    bool l_down = glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS;
    if (l_down && !s_l_last){
        editor.mode = (editor.mode == MODE_LIGHT) ? MODE_OBJECT : MODE_LIGHT;
        editor.selected_light_id = -1;
        std::cout << "[editor] mode -> " << (editor.mode == MODE_LIGHT ? "LIGHT" : "OBJECT") << "\n";
    }
    s_l_last = l_down;

    // Z to toggle audio editor mode
    // requires an object to be selected
    static bool s_z_last = false;
    bool z_down = glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
    if (z_down && !s_z_last){
        if (editor.mode == MODE_AUDIO){
            editor.mode = MODE_OBJECT;
            std::cout << "[editor] mode -> OBJECT\n";
        }
        else if (editor.selected_id != -1){
            editor.mode = MODE_AUDIO;
            editor.audio_slot = 0;
            editor.audio_file_page = 0;
            editor_scan_audio(editor, "../assets");
            std::cout << "[editor] mode -> AUDIO (id=" << editor.selected_id << ")\n";
        }
        else {
            std::cout << "[editor] audio mode requires a selected object\n";
        }
    }
    s_z_last = z_down;

    // I to toggle ambience editor mode
    static bool s_i_last = false;
    bool i_down = glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS;
    if (i_down && !s_i_last){
        if (editor.mode == MODE_AMBIENCE){
            editor.mode = MODE_OBJECT;
            std::cout << "[editor] mode -> OBJECT\n";
        }
        else {
            editor.mode = MODE_AMBIENCE;
            editor.selected_zone_id = -1;
            editor.ambience_placing = false;
            editor_scan_audio(editor, "../assets");
            std::cout << "[editor] mode -> AMBIENCE\n";
        }
    }
    s_i_last = i_down;

    // K to toggle pose editor mode
    static bool s_k_last = false;
    bool k_down = glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS;
    if (k_down && !s_k_last){
        if (editor.mode == MODE_POSE){
            editor.mode = MODE_OBJECT;
            // if we were editing an npc, restore driver pose seat
            // npc world pos gets written to pose_seat for hail preview
            // which corrupts the driver seat on return to drive mode
            if (editor.pose_npc_id != -1){
                std::ifstream pf("../assets/entity/driver_pose.txt");
                if (pf.is_open()){
                    std::string tag;
                    while (pf >> tag){
                        if (tag == "seat")
                            pf >> editor.pose_seat.x >> editor.pose_seat.y >> editor.pose_seat.z;
                        else if (tag == "quat"){
                            int idx; pf >> idx;
                            if (idx >= 0 && idx < 6)
                                pf >> editor.pose_quat[idx].w >> editor.pose_quat[idx].x
                                   >> editor.pose_quat[idx].y >> editor.pose_quat[idx].z;
                        }
                        else if (tag == "offset"){
                            int idx; pf >> idx;
                            if (idx >= 0 && idx < 6)
                                pf >> editor.pose_offset[idx].x >> editor.pose_offset[idx].y
                                   >> editor.pose_offset[idx].z;
                        }
                    }
                }
            }
            editor.pose_npc_id = -1;
            editor.pose_editing_hail = false;
            std::cout << "[editor] mode -> OBJECT\n";
        } 
        else {
                editor.mode = MODE_POSE;
            editor.pose_numpad_translate = false;
            for (int i = 0; i < 6; i++) editor.pose_quat[i] = glm::quat(1,0,0,0);
            for (int i = 0; i < 6; i++) editor.pose_offset[i] = glm::vec3(0.0f);
            editor.pose_seat = glm::vec3(
                Const::DRIVER_SEAT_OFFSET_X,
                Const::DRIVER_SEAT_OFFSET_Y,
                Const::DRIVER_SEAT_OFFSET_Z);

            // check if a pedestrian is selected 
            // auto-load it as pose target
            editor.pose_npc_id = -1;
            for (const auto& o : map.objects){
                if (o.id == editor.selected_id && o.behavior == PEDESTRIAN){
                    editor.pose_npc_id = o.id;
                    // load mount pose as starting point 
                    // Ctrl+H overwrites with hail if needed
                    for (int i = 0; i < 6; i++) editor.pose_quat[i] = o.npc_mount_quat[i];
                    for (int i = 0; i < 6; i++) editor.pose_offset[i] = o.npc_mount_offset[i];
                    editor.pose_seat = o.npc_mount_seat;
                    std::cout << "[pose] loaded mount pose from npc id=" << o.id << "\n";
                    break;
                }
            }

            // no npc selected 
            // fall back to driver pose file
            if (editor.pose_npc_id == -1){
                std::ifstream pf("../assets/entity/driver_pose.txt");
                if (pf.is_open()) {
                    std::string tag;
                    while (pf >> tag) {
                        if (tag == "seat") {
                            pf >> editor.pose_seat.x >> editor.pose_seat.y >> editor.pose_seat.z;
                        } 
                        else if (tag == "quat") {
                            int idx; pf >> idx;
                            if (idx >= 0 && idx < 6)
                                pf >> editor.pose_quat[idx].w >> editor.pose_quat[idx].x
                                   >> editor.pose_quat[idx].y >> editor.pose_quat[idx].z;
                        } 
                        else if (tag == "offset") {
                            int idx; pf >> idx;
                            if (idx >= 0 && idx < 6)
                                pf >> editor.pose_offset[idx].x >> editor.pose_offset[idx].y
                                   >> editor.pose_offset[idx].z;
                        }
                    }
                    std::cout << "[pose] loaded ../assets/entity/driver_pose.txt\n";
                }
            }
            std::cout << "[editor] mode -> POSE (target=" 
                      << (editor.pose_npc_id == -1 ? "driver" : "npc") << ")\n";
        }
    }
    s_k_last = k_down;

    // *******************************
    // AUDIO MODE
    // *******************************
    if (editor.mode == MODE_AUDIO){
        // slot names match WorldObject audio fields in order
        static const char* AUDIO_SLOT_NAMES[] = {
            "impact", "proximity",
            "hail", "pickup", "yap",
            "dropoff_good", "dropoff_bad",
            "crash_mild", "crash_heavy", "crash_rollover"
        };
        static constexpr int AUDIO_SLOT_COUNT = 10;
        static constexpr int AUDIO_PAGE_SIZE  = 8;

        // find selected object
        WorldObject* target = nullptr;
        for (auto& o : map.objects)
            if (o.id == editor.selected_id){ target = &o; break; }

        if (!target){
            editor.mode = MODE_OBJECT;
            return;
        }

        // helper: get pointer to the correct slot string by index
        auto get_slot = [&](WorldObject& o, int slot) -> std::string& {
            switch(slot){
                case 0: return o.audio_impact;
                case 1: return o.audio_proximity;
                case 2: return o.audio_hail;
                case 3: return o.audio_pickup;
                case 4: return o.audio_yap;
                case 5: return o.audio_dropoff_good;
                case 6: return o.audio_dropoff_bad;
                case 7: return o.audio_crash_mild;
                case 8: return o.audio_crash_heavy;
                case 9: return o.audio_crash_rollover;
                default: return o.audio_impact;
            }
        };

        // Tab cycles audio slots (within audio mode only, not the global tab)
        static bool s_audio_tab_last = false;
        bool tab_down = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tab_down && !s_audio_tab_last){
            editor.audio_slot = (editor.audio_slot + 1) % AUDIO_SLOT_COUNT;
            editor.audio_file_page = 0;
            std::cout << "[audio] slot -> " << AUDIO_SLOT_NAMES[editor.audio_slot] << "\n";
        }
        s_audio_tab_last = tab_down;

        // up/down scroll file list
        bool au = glfwGetKey(window, GLFW_KEY_UP)   == GLFW_PRESS;
        bool ad = glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS;
        if (au && !s_arr_up_last){
            editor.audio_file_page = std::max(0, editor.audio_file_page - 1);
        }
        if (ad && !s_arr_down_last){
            int max_page = std::max(0, (int)editor.audio_file_list.size() - AUDIO_PAGE_SIZE);
            editor.audio_file_page = std::min(max_page, editor.audio_file_page + 1);
        }
        s_arr_up_last  = au;
        s_arr_down_last = ad;

        // 1-8 select file from current page and assign to slot
        static const int num_keys[8] = {
            GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
            GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8
        };
        for (int i = 0; i < AUDIO_PAGE_SIZE; i++){
            if (glfwGetKey(window, num_keys[i]) == GLFW_PRESS){
                int idx = editor.audio_file_page + i;
                if (idx < (int)editor.audio_file_list.size()){
                    get_slot(*target, editor.audio_slot) = editor.audio_file_list[idx];
                    map_dirty = true;
                    std::cout << "[audio] slot=" << AUDIO_SLOT_NAMES[editor.audio_slot]
                              << " -> " << editor.audio_file_list[idx] << "\n";
                }
            }
        }

        // [ / ] adjust proximity radius when that slot is active
        if (editor.audio_slot == 1){
            bool brk_l = glfwGetKey(window, GLFW_KEY_LEFT_BRACKET)  == GLFW_PRESS;
            bool brk_r = glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS;
            if (brk_l){ target->audio_radius = std::max(1.0f, target->audio_radius - 4.0f * dt); map_dirty = true; }
            if (brk_r){ target->audio_radius = std::min(200.0f, target->audio_radius + 4.0f * dt); map_dirty = true; }
        }

        // DEL clears current slot
        bool del = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS;
        if (del && !s_del_last){
            get_slot(*target, editor.audio_slot).clear();
            map_dirty = true;
            std::cout << "[audio] slot=" << AUDIO_SLOT_NAMES[editor.audio_slot] << " cleared\n";
        }
        s_del_last = del;

        // Ctrl+S saves
        bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
        if (ctrl && glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            world_map_save(map, Const::MAP_SAVE_PATH);

        return;
    }

    // *******************************
    // AMBIENCE MODE
    // *******************************
    if (editor.mode == MODE_AMBIENCE){
        static constexpr int AMB_PAGE_SIZE = 8;
        bool ctrl  = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
        bool lmb   = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        // LMB: place new zone or select existing one
        if (lmb && !s_lmb_last && editor.placement_valid){
            // check if clicking near an existing zone center (within 2m)
            int closest_id = -1;
            float closest_dist = 2.0f;
            for (int i = 0; i < map.ambience_count; i++){
                glm::vec3 d = map.ambience_zones[i].pos - editor.ghost_pos;
                d.y = 0.0f;
                float dist = glm::length(d);
                if (dist < closest_dist){ closest_dist = dist; closest_id = map.ambience_zones[i].id; }
            }

            if (closest_id != -1){
                editor.selected_zone_id = closest_id;
                editor.ambience_file_page = 0;
                std::cout << "[ambience] selected zone id=" << closest_id << "\n";
            }
            else if (map.ambience_count < Const::MAX_AMBIENCE_ZONES){
                // place new zone
                AmbienceZone z;
                z.id = map.next_ambience_id++;
                z.pos = editor.ghost_pos;
                z.radius = Const::AMBIENCE_RADIUS_DEFAULT;
                z.type = AMBIENCE_PROXIMITY;
                z.night_only = false;
                map.ambience_zones[map.ambience_count++] = z;
                editor.selected_zone_id = z.id;
                editor.ambience_file_page = 0;
                map_dirty = true;
                std::cout << "[ambience] placed zone id=" << z.id << "\n";
            }
        }
        s_lmb_last = lmb;

        // operate on selected zone
        AmbienceZone* zone = nullptr;
        for (int i = 0; i < map.ambience_count; i++)
            if (map.ambience_zones[i].id == editor.selected_zone_id){ zone = &map.ambience_zones[i]; break; }

        if (zone){
            // [ / ] shrink / grow radius
            bool brk_l = glfwGetKey(window, GLFW_KEY_LEFT_BRACKET)  == GLFW_PRESS;
            bool brk_r = glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS;
            if (brk_l){ zone->radius = std::max(Const::AMBIENCE_RADIUS_MIN, zone->radius - Const::AMBIENCE_RADIUS_STEP); map_dirty = true; }
            if (brk_r){ zone->radius = std::min(Const::AMBIENCE_RADIUS_MAX, zone->radius + Const::AMBIENCE_RADIUS_STEP); map_dirty = true; }

            // F cycle type: PROXIMITY <-> NIGHT
            static bool s_amb_f_last = false;
            bool f_down = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
            if (f_down && !s_amb_f_last){
                zone->type = (zone->type == AMBIENCE_PROXIMITY) ? AMBIENCE_NIGHT : AMBIENCE_PROXIMITY;
                zone->night_only = (zone->type == AMBIENCE_NIGHT);
                map_dirty = true;
                std::cout << "[ambience] zone id=" << zone->id
                          << " type -> " << (zone->type == AMBIENCE_NIGHT ? "NIGHT" : "PROXIMITY") << "\n";
            }
            s_amb_f_last = f_down;

            // up/down scroll audio file list
            bool au = glfwGetKey(window, GLFW_KEY_UP)   == GLFW_PRESS;
            bool ad = glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS;
            if (au && !s_arr_up_last)
                editor.ambience_file_page = std::max(0, editor.ambience_file_page - 1);
            if (ad && !s_arr_down_last){
                int max_page = std::max(0, (int)editor.audio_file_list.size() - AMB_PAGE_SIZE);
                editor.ambience_file_page = std::min(max_page, editor.ambience_file_page + 1);
            }
            s_arr_up_last  = au;
            s_arr_down_last = ad;

            // 1-8 assign audio file to zone
            static const int num_keys[8] = {
                GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
                GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8
            };
            for (int i = 0; i < AMB_PAGE_SIZE; i++){
                if (glfwGetKey(window, num_keys[i]) == GLFW_PRESS){
                    int idx = editor.ambience_file_page + i;
                    if (idx < (int)editor.audio_file_list.size()){
                        strncpy(zone->audio_path, editor.audio_file_list[idx].c_str(), 255);
                        zone->audio_path[255] = '\0';
                        map_dirty = true;
                        std::cout << "[ambience] zone id=" << zone->id
                                  << " audio -> " << zone->audio_path << "\n";
                    }
                }
            }

            // DEL delete selected zone
            bool del = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS;
            if (del && !s_del_last){
                // compact the array
                for (int i = 0; i < map.ambience_count; i++){
                    if (map.ambience_zones[i].id != editor.selected_zone_id) continue;
                    map.ambience_zones[i] = map.ambience_zones[--map.ambience_count];
                    break;
                }
                editor.selected_zone_id = -1;
                map_dirty = true;
                std::cout << "[ambience] zone deleted\n";
            }
            s_del_last = del;
        }

        // Ctrl+S saves both map and ambience file
        if (ctrl && glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS){
            world_map_save(map, Const::MAP_SAVE_PATH);
            ambience_save(map.ambience_zones, map.ambience_count, Const::AMBIENCE_SAVE_PATH);
        }

        return;
    }

    // *******************************
    // POSE MODE
    // *******************************
    if (editor.mode == MODE_POSE){

        // LMB: click a pedestrian to switch pose target mid-session
        bool lmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (lmb && !s_lmb_last){
            int hit = editor_raycast_objects(mx, my, view, proj, screen_w, screen_h, map, er, false);
            if (hit != -1){
                for (const auto& o : map.objects){
                    if (o.id != hit || o.behavior != PEDESTRIAN) continue;
                    editor.pose_npc_id = o.id;
                    editor.selected_id = o.id;
                    for (int i = 0; i < 6; i++) editor.pose_quat[i] = o.npc_mount_quat[i];
                    for (int i = 0; i < 6; i++) editor.pose_offset[i] = o.npc_mount_offset[i];
                    editor.pose_seat = o.npc_mount_seat;
                    std::cout << "[pose] switched to npc id=" << o.id << "\n";
                    break;
                }
            }
        }
        s_lmb_last = lmb;

        bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
        float rot_speed = glm::radians(60.0f * dt);
        if (shift) rot_speed *= 0.1f;

        // F cycles active bone
        static bool s_pose_f_last = false;
        bool f_down = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (f_down && !s_pose_f_last){
            editor.pose_bone = (editor.pose_bone + 1) % 6;
            static const char* bone_names[6] = {
                "TORSO", "HEAD", "LEG_L", "LEG_R", "ARM_L", "ARM_R"
            };
            std::cout << "[pose] bone -> " << bone_names[editor.pose_bone] << "\n";
        }
        s_pose_f_last = f_down;

        // V toggles between hail and mount pose editing
        static bool s_pose_v_last = false;
        bool v_down = glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS;
        if (v_down && !s_pose_v_last && editor.pose_npc_id != -1){
            editor.pose_editing_hail = !editor.pose_editing_hail;
            // load the target pose so you start from existing saved data
            for (const auto& o : map.objects){
                if (o.id != editor.pose_npc_id) continue;
                if (editor.pose_editing_hail){
                    // if no hail pose saved yet, start from identity (standing)
                    // seat at NPC's editor position so it renders in place
                    bool hail_empty = (glm::length(o.npc_hail_seat) < 0.001f);
                    if (hail_empty){
                        for (int i = 0; i < 6; i++) editor.pose_quat[i]   = glm::quat(1,0,0,0);
                        for (int i = 0; i < 6; i++) editor.pose_offset[i] = glm::vec3(0.0f);
                        editor.pose_seat = o.position;
                        std::cout << "[pose] HAIL pose empty, starting from standing at editor pos\n";
                    } 
                    else {
                        for (int i = 0; i < 6; i++) editor.pose_quat[i] = o.npc_hail_quat[i];
                        for (int i = 0; i < 6; i++) editor.pose_offset[i] = o.npc_hail_offset[i];
                        editor.pose_seat = o.npc_hail_seat;
                    }
                    std::cout << "[pose] switched to HAIL pose\n";
                } 
                else {
                    bool mount_empty = (glm::length(o.npc_mount_seat) < 0.001f);
                    if (mount_empty){
                        for (int i = 0; i < 6; i++) editor.pose_quat[i]   = glm::quat(1,0,0,0);
                        for (int i = 0; i < 6; i++) editor.pose_offset[i] = glm::vec3(0.0f);
                        editor.pose_seat = o.position;
                        std::cout << "[pose] MOUNT pose empty, starting from standing at editor pos\n";
                    } 
                    else {
                        for (int i = 0; i < 6; i++) editor.pose_quat[i]   = o.npc_mount_quat[i];
                        for (int i = 0; i < 6; i++) editor.pose_offset[i] = o.npc_mount_offset[i];
                        editor.pose_seat = o.npc_mount_seat;
                    }
                    std::cout << "[pose] switched to MOUNT pose\n";
                }
                break;
            }
        }
        s_pose_v_last = v_down;

        glm::quat& q = editor.pose_quat[editor.pose_bone];

        // incremental rotation in bone local space — no gimbal
        // each press rotates around a fixed local axis
        // left/right = Y (twist), up/down = X (bend fwd/back), pgup/dn = Z (lean)
        if (!editor.pose_numpad_translate) {
            if (glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS)
                q = glm::angleAxis(-rot_speed, glm::vec3(1,0,0)) * q;
            if (glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS)
                q = glm::angleAxis( rot_speed, glm::vec3(1,0,0)) * q;
            if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS)
                q = glm::angleAxis(-rot_speed, glm::vec3(0,1,0)) * q;
            if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
                q = glm::angleAxis( rot_speed, glm::vec3(0,1,0)) * q;
            if (glfwGetKey(window, GLFW_KEY_PAGE_UP)   == GLFW_PRESS)
                q = glm::angleAxis( rot_speed, glm::vec3(0,0,1)) * q;
            if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS)
                q = glm::angleAxis(-rot_speed, glm::vec3(0,0,1)) * q;
        }

        // numpad 0 toggles between seat nudge and bone translate
        static bool s_kp0_last = false;
        bool kp0 = glfwGetKey(window, GLFW_KEY_KP_0) == GLFW_PRESS;
        if (kp0 && !s_kp0_last){
            editor.pose_numpad_translate = !editor.pose_numpad_translate;
            std::cout << "[pose] numpad -> "
                      << (editor.pose_numpad_translate ? "BONE TRANSLATE" : "SEAT") << "\n";
        }
        s_kp0_last = kp0;

        float nudge_speed = 8.0f * dt;
        if (shift) nudge_speed *= 0.1f;

        if (editor.pose_numpad_translate){
            // bone translate mode: move active bone mesh in model space
            glm::vec3& off = editor.pose_offset[editor.pose_bone];
            if (glfwGetKey(window, GLFW_KEY_KP_8) == GLFW_PRESS) off.z -= nudge_speed;
            if (glfwGetKey(window, GLFW_KEY_KP_2) == GLFW_PRESS) off.z += nudge_speed;
            if (glfwGetKey(window, GLFW_KEY_KP_4) == GLFW_PRESS) off.x -= nudge_speed;
            if (glfwGetKey(window, GLFW_KEY_KP_6) == GLFW_PRESS) off.x += nudge_speed;
            if (glfwGetKey(window, GLFW_KEY_KP_ADD)      == GLFW_PRESS) off.y += nudge_speed;
            if (glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS) off.y -= nudge_speed;
        } 
        else {
            // seat mode: move entire driver
            if (glfwGetKey(window, GLFW_KEY_KP_8) == GLFW_PRESS) editor.pose_seat.z -= nudge_speed;
            if (glfwGetKey(window, GLFW_KEY_KP_2) == GLFW_PRESS) editor.pose_seat.z += nudge_speed;
            if (glfwGetKey(window, GLFW_KEY_KP_4) == GLFW_PRESS) editor.pose_seat.x -= nudge_speed;
            if (glfwGetKey(window, GLFW_KEY_KP_6) == GLFW_PRESS) editor.pose_seat.x += nudge_speed;
            if (glfwGetKey(window, GLFW_KEY_KP_ADD)      == GLFW_PRESS) editor.pose_seat.y += nudge_speed;
            if (glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS) editor.pose_seat.y -= nudge_speed;
        }

        // Enter: dump as axis-angle, paste into driver_anim.cpp
        static bool s_pose_enter_last = false;
        bool enter = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
        if (enter && !s_pose_enter_last){
            static const char* bone_names[6] = {
                "BONE_TORSO", "BONE_HEAD", "BONE_LEG_L", "BONE_LEG_R", "BONE_ARM_L", "BONE_ARM_R"
            };
            std::cout << "\n// --- paste into pose_sit() in driver_anim.cpp ---\n";
            for (int i = 0; i < 6; i++){
                glm::quat& bq = editor.pose_quat[i];
                // skip identity bones
                if (std::abs(bq.w - 1.0f) < 0.001f) continue;
                float angle = 2.0f * std::acos(glm::clamp(bq.w, -1.0f, 1.0f));
                float s = std::sqrt(1.0f - bq.w * bq.w);
                glm::vec3 axis = (s > 0.001f)
                    ? glm::vec3(bq.x/s, bq.y/s, bq.z/s)
                    : glm::vec3(1,0,0);
                std::cout << "pose.local[" << bone_names[i] << "] = "
                          << "glm::rotate(pose.local[" << bone_names[i] << "], "
                          << angle << "f, glm::vec3("
                          << axis.x << "f, " << axis.y << "f, " << axis.z << "f));\n";
            }
           // bone offsets
            for (int i = 0; i < 6; i++){
                glm::vec3& off = editor.pose_offset[i];
                if (glm::length(off) < 0.001f) continue;
                std::cout << "// " << bone_names[i] << " offset\n";
                std::cout << "bone_local = glm::translate(bone_local, glm::vec3("
                          << off.x << "f, " << off.y << "f, " << off.z << "f));\n";
            }
            std::cout << "\n// --- paste into const.hpp ---\n";
            std::cout << "inline constexpr float DRIVER_SEAT_OFFSET_X = "
                      << editor.pose_seat.x << "f;\n";
            std::cout << "inline constexpr float DRIVER_SEAT_OFFSET_Y = "
                      << editor.pose_seat.y << "f;\n";
            std::cout << "inline constexpr float DRIVER_SEAT_OFFSET_Z = "
                      << editor.pose_seat.z << "f;\n\n";
        }

        s_pose_enter_last = enter;

        bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;

        // Ctrl+H  save current pose as hail pose for selected NPC
        static bool s_pose_h_last = false;
        bool h_down = glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS;
        if (ctrl && h_down && !s_pose_h_last && editor.pose_npc_id != -1){
            for (auto& o : map.objects){
                if (o.id != editor.pose_npc_id || o.behavior != PEDESTRIAN) continue;                for (int i = 0; i < 6; i++){
                    o.npc_hail_quat[i] = editor.pose_quat[i];
                    o.npc_hail_offset[i] = editor.pose_offset[i];
                }
                o.npc_hail_seat = editor.pose_seat;
                map_dirty = true;
                std::cout << "[pose] hail pose saved to npc id=" << o.id << "\n";
                break;
            }
        }
        s_pose_h_last = h_down;

        // Ctrl+M save current pose as mount/passenger pose for selected NPC
        static bool s_pose_m_last = false;
        bool pose_m_down = glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS;
        if (ctrl && pose_m_down && !s_pose_m_last && editor.pose_npc_id != -1){
            for (auto& o : map.objects){
                if (o.id != editor.pose_npc_id || o.behavior != PEDESTRIAN) continue;                for (int i = 0; i < 6; i++){
                    o.npc_mount_quat[i]   = editor.pose_quat[i];
                    o.npc_mount_offset[i] = editor.pose_offset[i];
                }
                o.npc_mount_seat = editor.pose_seat;
                map_dirty = true;
                std::cout << "[pose] mount pose saved to npc id=" << o.id << "\n";
                break;
            }
        }
        s_pose_m_last = pose_m_down;

        // Ctrl+S saves driver pose to file
        static bool s_pose_s_last = false;
        bool s_down = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
        if (ctrl && s_down && !s_pose_s_last && editor.pose_npc_id == -1) {
            std::ofstream pf("../assets/entity/driver_pose.txt");
            if (pf.is_open()) {
                pf << "seat " << editor.pose_seat.x << " "
                   << editor.pose_seat.y << " " << editor.pose_seat.z << "\n";
                for (int i = 0; i < 6; i++)
                    pf << "quat " << i << " "
                       << editor.pose_quat[i].w << " " << editor.pose_quat[i].x << " "
                       << editor.pose_quat[i].y << " " << editor.pose_quat[i].z << "\n";
                for (int i = 0; i < 6; i++)
                    pf << "offset " << i << " "
                       << editor.pose_offset[i].x << " "
                       << editor.pose_offset[i].y << " "
                       << editor.pose_offset[i].z << "\n";
                std::cout << "[pose] saved ../assets/entity/driver_pose.txt\n";
            }
        }
        s_pose_s_last = s_down;

        return;
    }


    // *******************************
    // TERRAIN MODE
    // *******************************
    if (editor.mode == MODE_TERRAIN){
        bool lmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
        bool rmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

        // [ and ] adjust brush radius in terrain mode
        bool brk_l = glfwGetKey(window, GLFW_KEY_LEFT_BRACKET)  == GLFW_PRESS;
        bool brk_r = glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS;
        if (brk_l) editor.brush_radius = std::max(Const::TERRAIN_BRUSH_RADIUS_MIN, editor.brush_radius - 6.0f * dt);
        if (brk_r) editor.brush_radius = std::min(Const::TERRAIN_BRUSH_RADIUS_MAX, editor.brush_radius + 6.0f * dt);

        // *******************************
        // PAINT MODE
        // *******************************
        if (editor.paint_mode){
            static const int surf_keys[7] = {
                GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3,
                GLFW_KEY_4, GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7
            };
            for (int i = 0; i < 7; i++){
                if (glfwGetKey(window, surf_keys[i]) == GLFW_PRESS)
                    editor.paint_surface = (SurfaceType)(i + 1);
            }
            if (glfwGetKey(window, GLFW_KEY_0) == GLFW_PRESS)
                editor.paint_surface = SURFACE_NONE;

            // Ctrl+Shift+W wipes entire surface map back to blank canvas
            static bool s_wipe_last = false;
            bool wipe = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                     && glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)   == GLFW_PRESS
                     && glfwGetKey(window, GLFW_KEY_W)            == GLFW_PRESS;
            if (wipe && !s_wipe_last){
                map.terrain.surface.assign(map.terrain.rows * map.terrain.cols, (uint8_t)SURFACE_NONE);
                er.terrain_surface_dirty = true;
                std::cout << "[paint] canvas wiped\n";
            }
            s_wipe_last = wipe;
        }

        if (editor.placement_valid){
            float cx = editor.ghost_pos.x;
            float cz = editor.ghost_pos.z;
            static bool s_sculpt_pushed = false;
            bool sculpting = (lmb || rmb) && !shift && !editor.paint_mode;
            bool smoothing = (lmb || rmb) &&  shift && !editor.paint_mode;
            if ((sculpting || smoothing) && !s_sculpt_pushed){
                heightfield_push_undo(map.terrain);
                s_sculpt_pushed = true;
            }
            if (!lmb && !rmb) s_sculpt_pushed = false;

            if (editor.paint_mode){
                if (lmb){
                    if (!s_sculpt_pushed){
                        heightfield_push_undo(map.terrain);
                        s_sculpt_pushed = true;
                    }
                    heightfield_paint(map.terrain, cx, cz,
                        editor.brush_radius, editor.paint_surface);
                    er.terrain_surface_dirty = true;
                    er.terrain_mesh_dirty    = false;
                }
            }
            else {
                if (shift){
                    // shift + any button = smooth brush
                    if (lmb || rmb)
                        heightfield_smooth(map.terrain, cx, cz,
                            editor.brush_radius, Const::TERRAIN_BRUSH_SMOOTH_STRENGTH * dt);
                }
                else if (lmb){
                    bool fine = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
                    float strength = fine ? Const::TERRAIN_BRUSH_STRENGTH * 0.1f : Const::TERRAIN_BRUSH_STRENGTH;
                    heightfield_sculpt(map.terrain, cx, cz,
                        editor.brush_radius, strength * dt * 60.0f);
                }
                else if (rmb){
                    bool fine = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
                    float strength = fine ? Const::TERRAIN_BRUSH_STRENGTH * 0.1f : Const::TERRAIN_BRUSH_STRENGTH;
                    heightfield_sculpt(map.terrain, cx, cz,
                        editor.brush_radius, -strength * dt * 60.0f);
                }

                // clamp terrain to designed limits after every sculpt
                if (lmb || rmb)
                    heightfield_clamp(map.terrain,
                        Const::TERRAIN_MIN_Y, Const::TERRAIN_MAX_Y);

                // mark terrain mesh for rebuild next draw
                if (lmb || rmb) er.terrain_mesh_dirty = true;
            }
        }

        bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
        bool shift_held = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

        // Ctrl+Z undo last sculpt stroke
        static bool s_z_last = false;
        bool z_down = glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
        if (ctrl && z_down && !s_z_last){
            heightfield_pop_undo(map.terrain);
            er.terrain_mesh_dirty    = true;
            er.terrain_surface_dirty = true;
            std::cout << "[terrain] undo stack remaining=" << map.terrain.undo_stack.size() << "\n";
        }
        s_z_last = z_down;

        // Ctrl+Shift+F flatten entire terrain to y=0
        static bool s_f_last = false;
        bool f_down = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (ctrl && shift_held && f_down && !s_f_last){
            heightfield_push_undo(map.terrain); // allow undoing the flatten too
            heightfield_flatten(map.terrain);
            er.terrain_mesh_dirty = true;
            std::cout << "[terrain] flattened\n";
        }
        s_f_last = f_down;

        // Ctrl+S saves
        if (ctrl && glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            world_map_save(map, Const::MAP_SAVE_PATH);

        return;
    }

    //*******************************
    // ROAD MODE
    //********************************
    if (editor.mode == MODE_ROAD){
        bool lmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
        bool rmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        bool enter = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
        bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

        // find or create the active spline
        RoadSpline* active = nullptr;
        if (editor.active_road_id != -1){
            for (auto& r : map.roads)
                if (r.id == editor.active_road_id){ active = &r; break; }
        }

        // LMB = add control point to active spline
        if (lmb && !s_lmb_last && editor.placement_valid){
            if (!active){
                // start a new spline
                RoadSpline nr;
                nr.id    = map.next_road_id++;
                nr.type  = ROAD_ASPHALT;
                nr.width = 7.0f;
                map.roads.push_back(nr);
                active = &map.roads.back();
                editor.active_road_id = nr.id;
                std::cout << "[road] new spline id=" << nr.id << "\n";
            }
            glm::vec3 pt = editor.ghost_pos;
            active->points.push_back(pt);
            road_spline_build_mesh(*active, &map.terrain);
            std::cout << "[road] added point (" << pt.x << "," << pt.y << "," << pt.z
                      << ") total=" << active->points.size() << "\n";
        }

        // PgUp/PgDn nudge selected point Y
        // fine-tune height independent of terrain
        if (active && editor.selected_point_idx >= 0
            && editor.selected_point_idx < (int)active->points.size())
        {
            bool pgup = glfwGetKey(window, GLFW_KEY_PAGE_UP)   == GLFW_PRESS;
            bool pgdn = glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS;
            float step = shift ? 0.05f : 0.25f;
            if (pgup){ active->points[editor.selected_point_idx].y += step; road_spline_build_mesh(*active); }
            if (pgdn){ active->points[editor.selected_point_idx].y -= step; road_spline_build_mesh(*active); }
        }

        // [ / ] cycle road type on active spline
        bool brk_l = glfwGetKey(window, GLFW_KEY_LEFT_BRACKET)  == GLFW_PRESS;
        bool brk_r = glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS;
        static bool s_brk_l_last = false, s_brk_r_last = false;
        if (active){
            if (brk_l && !s_brk_l_last){
                active->type = (RoadType)(((int)active->type - 1 + ROAD_COUNT) % ROAD_COUNT);
                active->width = (active->type == ROAD_LINES) ? 0.3f : 7.0f;
                road_spline_build_mesh(*active, &map.terrain);
                std::cout << "[road] type -> " << (int)active->type << "\n";
            }
            if (brk_r && !s_brk_r_last){
                active->type = (RoadType)(((int)active->type + 1) % ROAD_COUNT);
                active->width = (active->type == ROAD_LINES) ? 0.3f : 7.0f;
                road_spline_build_mesh(*active, &map.terrain);
                std::cout << "[road] type -> " << (int)active->type << "\n";
            }
        }
        s_brk_l_last = brk_l;
        s_brk_r_last = brk_r;

        // Enter = finish spline, return to object mode
        if (enter && !s_enter_last){
            if (active && active->points.size() >= 2)
                road_spline_build_mesh(*active, &map.terrain);
            editor.active_road_id    = -1;
            editor.selected_point_idx = -1;
            editor.mode = MODE_OBJECT;
            std::cout << "[road] spline finished\n";
        }
        s_enter_last = enter;

        // RMB = undo last point
        if (rmb && !s_rmb_last && active && !active->points.empty()){
            active->points.pop_back();
            if (active->points.size() >= 2) road_spline_build_mesh(*active);
            std::cout << "[road] removed last point, remaining=" << active->points.size() << "\n";
        }
        s_rmb_last = rmb;

        // DEL = delete entire active spline
        bool del = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS;
        if (del && !s_del_last && editor.active_road_id != -1){
            map.roads.erase(std::remove_if(map.roads.begin(), map.roads.end(),
                [&](const RoadSpline& r){ return r.id == editor.active_road_id; }),
                map.roads.end());
            editor.active_road_id = -1;
            std::cout << "[road] deleted spline\n";
        }
        s_del_last = del;

        // Ctrl+S saves in road mode too
        bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
        if (ctrl && glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            world_map_save(map, Const::MAP_SAVE_PATH);

        s_lmb_last = lmb;
        return;
    }

    // *******************************
    // OCEAN MODE
    // *******************************
    if (editor.mode == MODE_OCEAN){
        bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
        bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
        float step = shift ? 0.05f : 0.25f;

        // PgUp/PgDn nudge global ocean Y level
        bool pgup = glfwGetKey(window, GLFW_KEY_PAGE_UP)   == GLFW_PRESS;
        bool pgdn = glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS;
        if (pgup && !s_pgup_last){
            map.ocean.y_level += step;
            map.ocean.mesh_dirty = true;
            er.terrain_surface_dirty = true;
        }
        if (pgdn && !s_pgdn_last){
            map.ocean.y_level -= step;
            map.ocean.mesh_dirty = true;
            er.terrain_surface_dirty = true;
        }
        s_pgup_last = pgup;
        s_pgdn_last = pgdn;

        // E toggles ocean on/off
        static bool s_e_last = false;
        bool e_down = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
        if (e_down && !s_e_last){
            map.ocean.enabled = !map.ocean.enabled;
            er.terrain_surface_dirty = true;
            std::cout << "[ocean] " << (map.ocean.enabled ? "enabled" : "disabled") << "\n";
        }
        s_e_last = e_down;

        if (ctrl && glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            world_map_save(map, Const::MAP_SAVE_PATH);

        return;
    }

    // *******************************
    // LIGHT MODE
    // *******************************
    if (editor.mode == MODE_LIGHT){
        bool lmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
        bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)  == GLFW_PRESS;

        // LMB: try select existing light first, else place new one
        if (lmb && !s_lmb_last && editor.placement_valid){
            // find closest light within 2m of click pos
            int closest_id = -1;
            float closest_dist = 2.0f;
            for (const auto& l : map.lights){
                glm::vec3 d = l.position - editor.ghost_pos;
                d.y = 0.0f;
                float dist = glm::length(d);
                if (dist < closest_dist){ closest_dist = dist; closest_id = l.id; }
            }

            if (closest_id != -1){
                // select existing light and load its values into staging
                editor.selected_light_id = closest_id;
                for (const auto& l : map.lights){
                    if (l.id != closest_id) continue;
                    editor.light_edit_color = l.color;
                    editor.light_edit_radius = l.radius;
                    editor.light_edit_intensity = l.intensity;
                    break;
                }
                std::cout << "[light] selected id=" << closest_id << "\n";
            }
            else {
                // place new light at cursor
                LightSource l;
                l.position  = editor.ghost_pos;
                l.position.y += 3.0f; // default streetlight height
                l.color = editor.light_edit_color;
                l.radius = editor.light_edit_radius;
                l.intensity = editor.light_edit_intensity;
                world_map_light_place(map, l);
                editor.selected_light_id = map.lights.back().id;
                map_dirty = true;
                std::cout << "[light] placed id=" << editor.selected_light_id
                          << " at (" << l.position.x << "," << l.position.z << ")\n";
            }
        }
        s_lmb_last = lmb;

        // edit selected light properties
        if (editor.selected_light_id != -1){
            for (auto& l : map.lights){
                if (l.id != editor.selected_light_id) continue;

                float step = shift ? 0.05f : 0.25f;

                // PgUp/PgDn nudge Y
                bool pgup = glfwGetKey(window, GLFW_KEY_PAGE_UP)   == GLFW_PRESS;
                bool pgdn = glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS;
                if (pgup && !s_pgup_last){ l.position.y += step; map_dirty = true; }
                if (pgdn && !s_pgdn_last){ l.position.y -= step; map_dirty = true; }
                s_pgup_last = pgup;
                s_pgdn_last = pgdn;

                // arrow keys move XZ
                bool al = glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS;
                bool ar = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;
                bool au = glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS;
                bool ad = glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS;
                if (al && !s_arr_left_last)  { l.position.x -= step; map_dirty = true; }
                if (ar && !s_arr_right_last) { l.position.x += step; map_dirty = true; }
                if (au && !s_arr_up_last)    { l.position.z -= step; map_dirty = true; }
                if (ad && !s_arr_down_last)  { l.position.z += step; map_dirty = true; }
                s_arr_left_last  = al; s_arr_right_last = ar;
                s_arr_up_last    = au; s_arr_down_last  = ad;

                // [ / ] adjust radius
                bool brk_l = glfwGetKey(window, GLFW_KEY_LEFT_BRACKET)  == GLFW_PRESS;
                bool brk_r = glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS;
                if (brk_l){ l.radius = std::max(1.0f, l.radius - 4.0f * dt); map_dirty = true; }
                if (brk_r){ l.radius = std::min(80.0f, l.radius + 4.0f * dt); map_dirty = true; }

                // + / - adjust intensity (using = and - keys)
                bool plus  = glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS;
                bool minus = glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS;
                if (plus)  { l.intensity = std::min(10.0f, l.intensity + 1.5f * dt); map_dirty = true; }
                if (minus) { l.intensity = std::max(0.0f, l.intensity - 1.5f * dt); map_dirty = true; }

                // R/G/B tint with numpad or Q/E/Z
                // Q/E = red down/up, Z/X = green, C/V = blue
                if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) l.color.r = std::max(0.0f, l.color.r - dt);
                if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) l.color.r = std::min(1.0f, l.color.r + dt);
                if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) l.color.g = std::max(0.0f, l.color.g - dt);
                if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS) l.color.g = std::min(1.0f, l.color.g + dt);
                if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS) l.color.b = std::max(0.0f, l.color.b - dt);
                if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS) l.color.b = std::min(1.0f, l.color.b + dt);

                // sync staging to live values
                editor.light_edit_color = l.color;
                editor.light_edit_radius = l.radius;
                editor.light_edit_intensity = l.intensity;
                break;
            }

            // DEL deletes selected light
            bool del = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS;
            if (del && !s_del_last){
                world_map_light_remove(map, editor.selected_light_id);
                editor.selected_light_id = -1;
                map_dirty = true;
                std::cout << "[light] deleted\n";
            }
            s_del_last = del;
        }

        if (ctrl && glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            world_map_save(map, Const::MAP_SAVE_PATH);

        return;
    }

    // prop palette
    // num keys 1-9 select from curr page of prop_list
    // page flips with [ and ] so we scroll past 9 props
    {
        // resolve active list first
        // PEDESTRIAN selected = entity palette, else prop palette
        bool ped_selected = false;
        for (const auto& o : map.objects)
            if (o.id == editor.selected_id && o.behavior == PEDESTRIAN) { ped_selected = true; break; }

        const std::vector<std::string>& active_list = ped_selected ? editor.entity_list : editor.prop_list;
        int& active_page = ped_selected ? editor.entity_page : editor.prop_page;

        // page scroll 
        // operates on whichever list is active
        static bool s_pgup_last = false;
        static bool s_pgdn_last = false;
        bool pgup = glfwGetKey(window, GLFW_KEY_LEFT_BRACKET)  == GLFW_PRESS;
        bool pgdn = glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS;
        if (pgup && !s_pgup_last && active_page > 0) active_page--;
        if (pgdn && !s_pgdn_last){
            int list_size = (int)active_list.size();
            int max_page = list_size > 0 ? (list_size - 1) / Const::EDITOR_PAGE_SIZE : 0;
            if (active_page < max_page) active_page++;
        }
        s_pgup_last = pgup;
        s_pgdn_last = pgdn;

        // 1-9 select from active list
        static const int num_keys[9] = {
            GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3,
            GLFW_KEY_4, GLFW_KEY_5, GLFW_KEY_6,
            GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9
        };

        for (int i = 0; i < Const::EDITOR_PAGE_SIZE; i++){
            if (glfwGetKey(window, num_keys[i]) == GLFW_PRESS){
                int idx = active_page * Const::EDITOR_PAGE_SIZE + i;
                if (idx < (int)active_list.size()){
                    editor.selected_model = active_list[idx];
                    if (!ped_selected) editor.selected_id = -1;
                    std::cout << "[editor] selected " << (ped_selected ? "entity" : "prop")
                              << ": " << editor.selected_model << "\n";
                }
            }
        }
    }

    // tool switching
    // tool key + <- -> to control
    // eg. R + <- -> to rotate mesh
    // probably should have added this to make it more clear
    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS) editor.tool = TOOL_TRANSLATE;
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) editor.tool = TOOL_ROTATE;
    if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS) editor.tool = TOOL_SCALE;

    // rotate selected object
    if (editor.selected_id != -1){
        for(auto& o : map.objects){
            if (o.id != editor.selected_id) continue;

            if (editor.tool == TOOL_TRANSLATE){
                bool al = glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS;
                bool ar = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;
                bool au = glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS;
                bool ad = glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS;
                bool pgup = glfwGetKey(window, GLFW_KEY_PAGE_UP) == GLFW_PRESS;
                bool pgdn = glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS;

                // alt held = fine mode: 5cm steps instead of grid snap
                bool alt = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
                float step = alt ? Const::EDITOR_GRID_SNAP_FINE : Const::EDITOR_GRID_SNAP;

                // xz movement
                if (al && !s_arr_left_last) o.position.x -= step;
                if (ar && !s_arr_right_last) o.position.x += step;
                if (au && !s_arr_up_last) o.position.z -= step;
                if (ad && !s_arr_down_last) o.position.z += step;

                // y nudge for vert pos adjustment
                if (pgup && !s_pgup_last) o.position.y += step;
                if (pgdn && !s_pgdn_last) o.position.y -= step;


                if (al || ar || au || ad || pgup || pgdn) map_dirty = true;
                s_arr_left_last = al;
                s_arr_right_last = ar;
                s_arr_up_last = au;
                s_arr_down_last = ad;
                s_pgup_last = pgup;
                s_pgdn_last = pgdn;
            }

            if (editor.tool == TOOL_ROTATE){
                if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) { o.rotation.y -= Const::EDITOR_ROTATE_SPEED * dt; map_dirty = true; }
                if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) { o.rotation.y += Const::EDITOR_ROTATE_SPEED * dt; map_dirty = true; }
            }

           if (editor.tool == TOOL_SCALE){
                bool alt = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
                float sspeed = alt ? Const::EDITOR_SCALE_SPEED_FINE : Const::EDITOR_SCALE_SPEED;
                bool sl = glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS;
                bool sr = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;
                if (sl) o.scale -= sspeed * dt;
                if (sr) o.scale += sspeed * dt;
                o.scale = glm::max(o.scale, glm::vec3(0.005f));
                if (sl || sr) map_dirty = true;
            }
            break;
        }
    }

    // LMB place object
    bool lmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    if (lmb && !s_lmb_last){

        // shift+click = force place on top of selected object, skip raycast
        // prevents buggy lmb spam clicking
        if (shift && editor.selected_id != -1 && editor.placement_valid && !editor.selected_model.empty()){
            // fall through to place block below with selected_id intact
        }
        // check first if we clicked an existing object
        else {
            // ctrl held = small object priority: picks smallest AABB volume hit
            // select smaller objects for easier placement
            // when putting smaller objects inside a bigger object
            bool ctrl_held = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
            int hit = editor_raycast_objects(mx, my, view, proj, screen_w, screen_h, map, er, ctrl_held);
            if (hit != -1){
                editor.selected_id = hit;
                std::cout << "editor selected id=" << hit << "\n";
                s_lmb_last = lmb;
                return;
            }
        }

        if(editor.placement_valid && !editor.selected_model.empty()){
            // no object hit
            // place new one
            WorldObject o;
            o.position = editor.ghost_pos;
            o.model_path = editor.selected_model;
            o.y_floor_offset = editor_get_y_floor_offset(er, editor.selected_model);

            // ground/road surfaces default to DECORATION — no collision
            // everything else defaults to STATIC
            // extend this list as new ground types are added
            static const char* GROUND_TYPES[] = {
                "asphalt", "gravel", "dirt", "sand", "grass", "cement"
            };
            o.behavior = STATIC;
            for (const char* gt : GROUND_TYPES){
                if (o.model_path.find(gt) != std::string::npos){
                    o.behavior = DECORATION;
                    break;
                }
            }

            // vertical stacking
            // scan objects sharing this XZ grid cell
            // if any exist just land on top of highes one
            float stack_y = 0.0f;
            if (editor.selected_id != -1){
                for (const auto& other : map.objects){
                    if (other.id != editor.selected_id) continue;

                    // compute world-space top using real mesh bounds when cached
                    float obj_height = other.scale.y;
                    auto bit = er.prop_bounds.find(other.model_path);
                    if (bit != er.prop_bounds.end()){
                        float yoff = er.prop_y_offset.count(other.model_path)
                            ? er.prop_y_offset.at(other.model_path) : 0.0f;
                        obj_height = (bit->second.local_max.y + yoff) * other.scale.y;
                    }
                    stack_y = other.position.y + obj_height;
                    break;
                }
            }
            o.position.y = stack_y;

            WorldObject& placed = world_map_place(map, o);
            editor.selected_id = placed.id;
            map_dirty = true;
        }
    }
    s_lmb_last = lmb;

    // Del remove selected object
    bool del = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS;
    if (del && !s_del_last){
        if (editor.selected_id != -1){
            world_map_remove(map, editor.selected_id);
            editor.selected_id = -1;
            map_dirty = true;
        }
    }
    
    // PEDESTRIAN config for selected object
    // only active when selected object is PEDESTRIAN
    if (editor.selected_id != -1){
        for (auto& o : map.objects){
            if (o.id != editor.selected_id) continue;
            if (o.behavior != PEDESTRIAN) break;

            bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
            float step = shift ? 0.05f : 0.5f;

            // J key cycles npc type
            static bool s_j_last = false;
            bool j_down = glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS;
            if (j_down && !s_j_last){
                o.npc_type = (o.npc_type + 1) % NPC_TYPE_COUNT;
                std::cout << "[npc] type -> " << NPC_TYPE_NAMES[o.npc_type] << "\n";
                map_dirty = true;
            }
            s_j_last = j_down;

            // G toggles can_hail (only meaningful for PERSON)
            static bool s_g_last = false;
            bool g_down = glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS;
            if (g_down && !s_g_last){
                o.npc_can_hail = !o.npc_can_hail;
                std::cout << "[npc] can_hail -> " << (o.npc_can_hail ? "YES" : "NO") << "\n";
                map_dirty = true;
            }
            s_g_last = g_down;

            // + / - adjust weight
            bool plus  = glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS;
            bool minus = glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS;
            if (plus)  { o.npc_weight += 5.0f * dt; map_dirty = true; }
            if (minus) { o.npc_weight = std::max(0.5f, o.npc_weight - 5.0f * dt); map_dirty = true; }

            // set walk_a / walk_b / drop_point from current ghost pos
            // I = set walk_a, U = set walk_b, X = set drop point
            static bool s_walka_last = false;
            static bool s_u_last = false;
            static bool s_x_last = false;
            bool walka_down = glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS;
            bool u_down = glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS;
            bool x_down = glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS;

            if (walka_down && !s_walka_last && editor.placement_valid){
                o.npc_walk_a = editor.ghost_pos;
                std::cout << "[npc] walk_a set to ("
                          << o.npc_walk_a.x << ", " << o.npc_walk_a.z << ")\n";
                map_dirty = true;
            }
            if (u_down && !s_u_last && editor.placement_valid){
                o.npc_walk_b = editor.ghost_pos;
                std::cout << "[npc] walk_b set to ("
                          << o.npc_walk_b.x << ", " << o.npc_walk_b.z << ")\n";
                map_dirty = true;
            }
            if (x_down && !s_x_last && editor.placement_valid){
                o.npc_drop_point = editor.ghost_pos;
                std::cout << "[npc] drop_point set to ("
                          << o.npc_drop_point.x << ", " << o.npc_drop_point.z << ")\n";
                map_dirty = true;
            }
            s_walka_last = walka_down;
            s_u_last = u_down;
            s_x_last = x_down;
            break;
        }
    }


    // B cycle behavior on selected object
    bool b_down = glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS;
    if (b_down && !s_b_last && editor.selected_id != -1){
        for (auto& o : map.objects){
            if (o.id != editor.selected_id) continue;
            switch (o.behavior){
                case STATIC:     o.behavior = DYNAMIC;     break;
                case DYNAMIC:    o.behavior = DECORATION;  break;
                case DECORATION: o.behavior = PEDESTRIAN;  break;
                case PEDESTRIAN: o.behavior = STATIC;      break;
            }
            // on becoming DYNAMIC apply current preset immediately
            if (o.behavior == DYNAMIC){
                const DynPreset& p = DYN_PRESETS[editor.dyn_preset_index];
                o.mass        = p.mass;
                o.restitution = p.restitution;
                o.friction    = p.friction;
                std::cout << "[editor] id=" << o.id << " DYNAMIC preset=" << p.name
                          << " mass=" << p.mass << "\n";
            } 
            else {
                o.mass = 999.0f;
                o.restitution = 0.10f;
                o.friction = 0.99f;
                std::cout << "[editor] id=" << o.id << " behavior -> " << o.behavior << "\n";
            }
            break;
        }
    }
    s_b_last = b_down;

    // N cycle dynamic preset on selected DYNAMIC object
    bool n_down = glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS;
    if (n_down && !s_n_last && editor.selected_id != -1){
        for (auto& o : map.objects){
            if (o.id != editor.selected_id) continue;
            if (o.behavior != DYNAMIC) break; // N only works on DYNAMIC objects
            editor.dyn_preset_index = (editor.dyn_preset_index + 1) % DYN_PRESET_COUNT;
            const DynPreset& p = DYN_PRESETS[editor.dyn_preset_index];
            o.mass        = p.mass;
            o.restitution = p.restitution;
            o.friction    = p.friction;
            std::cout << "[editor] id=" << o.id << " preset -> " << p.name
                      << " mass=" << p.mass << "\n";
            break;
        }
    }
    s_n_last = n_down;

    // Ctrl+C copy selected object into clipboard
    bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
    bool c_down = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
    bool v_down = glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS;

    if (ctrl && c_down && !s_c_last && editor.selected_id != -1){
        for (const auto& o : map.objects){
            if (o.id != editor.selected_id) continue;
            editor.clipboard     = o;
            editor.has_clipboard = true;
            std::cout << "[editor] copied id=" << o.id << " model=" << o.model_path << "\n";
            break;
        }
    }
    s_c_last = c_down;

    // Ctrl+V paste clipboard at current ghost pos
    if (ctrl && v_down && !s_v_last && editor.has_clipboard){
        if (editor.placement_valid){
            WorldObject o = editor.clipboard;
            o.position = editor.ghost_pos;
            // y stays at ghost ground level + whatever floor offset the model needs
            o.position.y = editor.clipboard.position.y; // preserve vertical nudge from original
            static const char* GROUND_TYPES[] = {
                "asphalt", "gravel", "dirt", "sand", "grass", "cement"
            };
            for (const char* gt : GROUND_TYPES){
                if (o.model_path.find(gt) != std::string::npos){
                    o.behavior = DECORATION;
                    break;
                }
            }
            
            WorldObject& placed = world_map_place(map, o);
            editor.selected_id  = placed.id;
            std::cout << "[editor] pasted " << o.model_path << " id=" << placed.id
                      << " at (" << o.position.x << ", " << o.position.z << ")\n";
        }
    }
    s_v_last = v_down;

    // save map
    if (ctrl && glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS){
        world_map_save(map, Const::MAP_SAVE_PATH);
    }

    // F5 rescan assets/ for new/removed props
    bool f5_down = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
    if (f5_down && !s_f5_last){
        editor_scan_props(editor, "../assets");
        editor.prop_page = 0;
        std::cout<< "[editor] assets refreshed total=" << editor.prop_list.size() << "\n";
     }
    s_f5_last = f5_down;
}
