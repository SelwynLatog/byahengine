#include "editor_input.hpp"
#include "const.hpp"
#include "../renderer/editor_renderer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <algorithm>
#include <iostream>

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
  [M]             exit road mode

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

    std::cout << "[editor] found " << editor.prop_list.size() << " props in"<< assets_dir<< "\n";
    for(const auto& p : editor.prop_list) std::cout<< " "<< p << "\n";
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

int editor_raycast_objects (double mx, double my, const glm::mat4& view, const glm::mat4& proj,
    int screen_w, int screen_h, const WorldMap& map, const EditorRenderer& er,
    bool prefer_small){

    float ndc_x = (2.0f * (float)mx / screen_w) - 1.0f;
    float ndc_y = 1.0f - (2.0f * (float)my / screen_h);

    glm::mat4 inv = glm::inverse(proj * view);
    glm::vec4 near_pt = inv * glm::vec4(ndc_x, ndc_y, -1.0f, 1.0f);
    glm::vec4 far_pt = inv * glm::vec4(ndc_x, ndc_y,  1.0f, 1.0f);
    near_pt /= near_pt.w;
    far_pt  /= far_pt.w;

    glm::vec3 ray_origin = glm::vec3(near_pt);
    glm::vec3 ray_dir = glm::normalize(glm::vec3(far_pt) - ray_origin);

    float closest_t   = 1e9f;
    float smallest_vol = 1e9f;
    int hit_id = -1;

    for (const auto& o : map.objects){
        glm::vec3 aabb_min, aabb_max;
        auto bit = er.prop_bounds.find(o.model_path);
        if (bit != er.prop_bounds.end()){
            float yoff = er.prop_y_offset.count(o.model_path)
                ? er.prop_y_offset.at(o.model_path) : 0.0f;
            glm::vec3 lmin = bit->second.local_min;
            glm::vec3 lmax = bit->second.local_max;
            aabb_min = o.position + glm::vec3(
                lmin.x * o.scale.x,
                (lmin.y + yoff) * o.scale.y,
                lmin.z * o.scale.z);
            aabb_max = o.position + glm::vec3(
                lmax.x * o.scale.x,
                (lmax.y + yoff) * o.scale.y,
                lmax.z * o.scale.z);
        } 
        else {
            glm::vec3 half = o.scale * 0.5f;
            aabb_min = o.position + glm::vec3(-half.x, 0.0f, -half.z);
            aabb_max = o.position + glm::vec3( half.x, o.scale.y,  half.z);
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
            // ctrl held: pick smallest AABB volume among all ray hits
            glm::vec3 sz = aabb_max - aabb_min;
            float vol = sz.x * sz.y * sz.z;
            if (vol < smallest_vol){
                smallest_vol = vol;
                hit_id = o.id;
            }
        } else {
            // normal: pick closest to camera
            if (tmin < closest_t){
                closest_t = tmin;
                hit_id = o.id;
            }
        }

        next_object:;
    }

    return hit_id;
}


void editor_input_update(EditorState& editor, WorldMap& map, EditorRenderer& er,
    GLFWwindow* window, const glm::mat4& view, const glm::mat4& proj,
    int screen_w, int screen_h, float dt){

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

    // H to toggle terain sculpt mode
    bool h_down = glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS;
    if (h_down && !s_h_last){
        editor.mode = (editor.mode == MODE_TERRAIN) ? MODE_OBJECT : MODE_TERRAIN;
        std::cout << "[editor] mode -> " << (editor.mode == MODE_TERRAIN ? "TERRAIN" : "OBJECT") << "\n";
    }
    s_h_last = h_down;

    // M to toggle to raod spline mode
    bool m_down = glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS;
    if (m_down && !s_m_last){
        editor.mode = (editor.mode == MODE_ROAD) ? MODE_OBJECT : MODE_ROAD;
        editor.road_placing = (editor.mode == MODE_ROAD);
        std::cout << "[editor] mode -> " << (editor.mode == MODE_ROAD ? "ROAD" : "OBJECT") << "\n";
    }
    s_m_last = m_down;

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

        if (editor.placement_valid){
            float cx = editor.ghost_pos.x;
            float cz = editor.ghost_pos.z;
            static bool s_sculpt_pushed = false;
            bool sculpting = (lmb || rmb) && !shift;
            bool smoothing = (lmb || rmb) && shift;
            if ((sculpting || smoothing) && !s_sculpt_pushed){
                heightfield_push_undo(map.terrain);
                s_sculpt_pushed = true;
            }
            if (!lmb && !rmb) s_sculpt_pushed = false;

            if (shift){
                // shift + any button = smooth brush
                if (lmb || rmb)
                    heightfield_smooth(map.terrain, cx, cz,
                        editor.brush_radius, Const::TERRAIN_BRUSH_SMOOTH_STRENGTH * dt);
            } 
            else if (lmb){
                // LMB = raise
                heightfield_sculpt(map.terrain, cx, cz,
                    editor.brush_radius, Const::TERRAIN_BRUSH_STRENGTH * dt * 60.0f);
            } 
            else if (rmb){
                // RMB = lower
                heightfield_sculpt(map.terrain, cx, cz,
                    editor.brush_radius, -Const::TERRAIN_BRUSH_STRENGTH * dt * 60.0f);
            }

            // clamp terrain to designed limits after every sculpt
            if (lmb || rmb)
                heightfield_clamp(map.terrain,
                    Const::TERRAIN_MIN_Y, Const::TERRAIN_MAX_Y);

            // mark terrain mesh for rebuild next draw
            if (lmb || rmb) er.terrain_mesh_dirty = true;
        }

        bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
        bool shift_held = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

        // Ctrl+Z undo last sculpt stroke
        static bool s_z_last = false;
        bool z_down = glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
        if (ctrl && z_down && !s_z_last){
            heightfield_pop_undo(map.terrain);
            er.terrain_mesh_dirty = true;
            std::cout << "[terrain] undo — stack remaining=" << map.terrain.undo_stack.size() << "\n";
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
            road_spline_build_mesh(*active);
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
                std::cout << "[road] type -> " << (int)active->type << "\n";
            }
            if (brk_r && !s_brk_r_last){
                active->type = (RoadType)(((int)active->type + 1) % ROAD_COUNT);
                std::cout << "[road] type -> " << (int)active->type << "\n";
            }
        }
        s_brk_l_last = brk_l;
        s_brk_r_last = brk_r;

        // Enter = finish spline, return to object mode
        if (enter && !s_enter_last){
            if (active && active->points.size() >= 2)
                road_spline_build_mesh(*active);
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

    // prop palette
    // num keys 1-9 select from curr page of prop_list
    // page flips with [ and ] so we scroll past 9 props
    {
        int total = (int)editor.prop_list.size();

        // page scroll
        // using bracket keys: [ = prev page, ] = next page
        static bool s_pgup_last = false;
        static bool s_pgdn_last = false;
        bool pgup = glfwGetKey(window, GLFW_KEY_LEFT_BRACKET)  == GLFW_PRESS;
        bool pgdn = glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS;
        if (pgup && !s_pgup_last && editor.prop_page > 0) editor.prop_page--;
        if (pgdn && !s_pgdn_last){
            int max_page = (total - 1) / Const::EDITOR_PAGE_SIZE;
            if (editor.prop_page < max_page) editor.prop_page++;
        }
        s_pgup_last = pgup;
        s_pgdn_last = pgdn;

        // 1-9 select prop on curr page
         static const int num_keys[9] = {
            GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3,
            GLFW_KEY_4, GLFW_KEY_5, GLFW_KEY_6,
            GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9
        };

        for (int i =0; i<Const::EDITOR_PAGE_SIZE; i++){
            if (glfwGetKey(window, num_keys[i]) == GLFW_PRESS){
                int idx = editor.prop_page * Const::EDITOR_PAGE_SIZE + i;
                if (idx < total){
                    editor.selected_model = editor.prop_list[idx];
                    editor.selected_id = -1; // deselect placed object when picking a new prop
                    std::cout << "[editor] selected prop: " << editor.selected_model << "\n";
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

                s_arr_left_last = al;
                s_arr_right_last = ar;
                s_arr_up_last = au;
                s_arr_down_last = ad;
                s_pgup_last = pgup;
                s_pgdn_last = pgdn;
            }

            if (editor.tool == TOOL_ROTATE){
                if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) o.rotation.y -= Const::EDITOR_ROTATE_SPEED * dt;
                if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) o.rotation.y += Const::EDITOR_ROTATE_SPEED * dt;
            }

            if (editor.tool == TOOL_SCALE){
                // alt held = fine scale: slow creep for small prop tuning
                bool alt = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
                float sspeed = alt ? Const::EDITOR_SCALE_SPEED_FINE : Const::EDITOR_SCALE_SPEED;
                if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) o.scale -= sspeed * dt;
                if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) o.scale += sspeed * dt;
                o.scale = glm::max(o.scale, glm::vec3(0.005f));
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
            std::cout << " editor placed " << o.model_path << " id = " << placed.id
            << " at (" << o.position.x << ", "<< o.position.z << ")\n";
        }
    }
    s_lmb_last = lmb;

    // Del remove selected object
    bool del = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS;
    if (del && !s_del_last){
        if (editor.selected_id != -1){
            world_map_remove(map, editor.selected_id);
            std::cout<< "editor removed id = "<< editor.selected_id<< "\n";
            editor.selected_id = -1;
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
            } else {
                o.mass        = 999.0f;
                o.restitution = 0.10f;
                o.friction    = 0.99f;
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
