#include "editor_input.hpp"
#include "const.hpp"
#include "../renderer/editor_renderer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <algorithm>
#include <iostream>

// key state tracking to prevent held key repeat
static bool s_tab_last = false;
static bool s_del_last = false;
static bool s_lmb_last = false;
static bool s_b_last = false;
static bool s_n_last = false;
static bool s_c_last = false;
static bool s_v_last = false;

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
    int screen_w, int screen_h, glm::vec3& out_pos){

    // convert screen pixel to NDC [-1, 1]
    float ndc_x = (2.0f * (float)mx / screen_w) - 1.0f;
    float ndc_y = 1.0f - (2.0f * (float)my / screen_h);

    // unproject two points to NDC z=-1 and z=1 to get ray direction
    glm::mat4 inv = glm::inverse(proj * view);

    glm::vec4 near_pt = inv * glm::vec4(ndc_x, ndc_y, -1.0f, 1.0f);
    glm::vec4 far_pt  = inv * glm::vec4(ndc_x, ndc_y,  1.0f, 1.0f);

    near_pt /= near_pt.w;
    far_pt /= far_pt.w;

    glm::vec3 ray_origin = glm::vec3(near_pt);
    glm::vec3 ray_dir = glm::normalize(glm::vec3(far_pt) - ray_origin);

    // intersect with y=0 ground plane
    // ray : P = origin + t * dir
    // solve for t when P.y = 0
    if (std::abs(ray_dir.y) < 1e-6f ) return false; // ray parallel to ground

    float t = -ray_origin.y / ray_dir.y;
    if (t < 0.0f) return false;  // intersection behind camera
    
    out_pos = ray_origin + ray_dir * t;
    return true;
}

int editor_raycast_objects (double mx, double my, const glm::mat4& view, const glm::mat4& proj,
    int screen_w, int screen_h, const WorldMap& map, const EditorRenderer& er){

    float ndc_x = (2.0f * (float)mx / screen_w) - 1.0f;
    float ndc_y = 1.0f - (2.0f * (float)my / screen_h);

    glm::mat4 inv = glm::inverse(proj * view);
    glm::vec4 near_pt = inv * glm::vec4(ndc_x, ndc_y, -1.0f, 1.0f);
    glm::vec4 far_pt = inv * glm::vec4(ndc_x, ndc_y,  1.0f, 1.0f);
    near_pt /= near_pt.w;
    far_pt  /= far_pt.w;

    glm::vec3 ray_origin = glm::vec3(near_pt);
    glm::vec3 ray_dir = glm::normalize(glm::vec3(far_pt) - ray_origin);

    float closest_t = 1e9f;
    int hit_id = -1;

    for (const auto& o : map.objects){
        // we build a simple AABB from pos + scale for picking
        // assume center bottom convention as it is pretty much used for all trike and obstacles
        // updated to use real mesh bounds when cached
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

        // ray vs AABB slab test
        float tmin = 0.0f, tmax = 1e9f;
        for (int i =0; i<3; i++){
            float inv_d = 1.0f / ray_dir[i];
            float t0 = (aabb_min[i] - ray_origin[i]) * inv_d;
            float t1 = (aabb_max[i] - ray_origin[i]) * inv_d;
            if (inv_d < 0.0f) std::swap(t0, t1);
            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);
            if (tmax < tmin) goto next_object;
            // goto is intentional
            // cleanest way to break out of nested loop without a bool flag or a lambda
            // goes nowhere weird and simply skips to next object
        }

        if (tmin < closest_t){
            closest_t = tmin;
            hit_id = o.id;
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
        mx, my, view, proj, screen_w, screen_h, editor.ghost_pos);
    
    // snap ghost to grid
    if (editor.placement_valid){
        editor.ghost_pos.x = std::round(editor.ghost_pos.x / Const::EDITOR_GRID_SNAP) * Const::EDITOR_GRID_SNAP;
        editor.ghost_pos.z = std::round(editor.ghost_pos.z / Const::EDITOR_GRID_SNAP) * Const::EDITOR_GRID_SNAP;
        editor.ghost_pos.y = 0.0f;
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

                // xz movement
                if (al && !s_arr_left_last) o.position.x -= Const::EDITOR_GRID_SNAP;
                if (ar && !s_arr_right_last) o.position.x += Const::EDITOR_GRID_SNAP;
                if (au && !s_arr_up_last) o.position.z -= Const::EDITOR_GRID_SNAP;
                if (ad && !s_arr_down_last) o.position.z += Const::EDITOR_GRID_SNAP;

                // y nudge for vert pos adjustment
                if (pgup && !s_pgup_last) o.position.y += Const::EDITOR_GRID_SNAP;
                if (pgdn && !s_pgdn_last) o.position.y -= Const::EDITOR_GRID_SNAP;

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
                if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) o.scale -= Const::EDITOR_SCALE_SPEED * dt;
                if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) o.scale += Const::EDITOR_SCALE_SPEED * dt;
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
            int hit = editor_raycast_objects(mx, my, view, proj, screen_w, screen_h, map, er);
            if (hit != -1){
                editor.selected_id = hit;
                std::cout << "editor selected id=" << hit << "\n";
                s_lmb_last = lmb;
                return; // early out, don't place
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
}
