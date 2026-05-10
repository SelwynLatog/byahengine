#include "editor_input.hpp"
#include "const.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

// key state tracking to prevent held key repeat
static bool s_tab_last = false;
static bool s_del_last = false;
static bool s_lmb_last = false;

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

void editor_input_update(EditorState & editor, WorldMap& map, GLFWwindow* window, 
    const glm::mat4& view, const glm::mat4& proj, int screen_w, int screen_h, float dt){

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

    // tool switching
    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS) editor.tool = TOOL_TRANSLATE;
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) editor.tool = TOOL_ROTATE;
    if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS) editor.tool = TOOL_SCALE;

    // rotate selected object
    if (editor.selected_id != -1){
        for(auto& o : map.objects){
            if (o.id != editor.selected_id) continue;

            if (editor.tool == TOOL_ROTATE){
                if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) o.rotation.y -= Const::EDITOR_ROTATE_SPEED * dt;
                if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) o.rotation.y += Const::EDITOR_ROTATE_SPEED * dt;
            }

            if (editor.tool == TOOL_SCALE){
                if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS) o.scale -= Const::EDITOR_SCALE_SPEED * dt;
                if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) o.scale += Const::EDITOR_SCALE_SPEED * dt;
                o.scale = glm::max(o.scale, glm::vec3(0.05f));
            }
            break;
        }
    }

    // LMB place object
    bool lmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (lmb && !s_lmb_last){
        if (editor.placement_valid && !editor.selected_model.empty()){
            WorldObject o;
            o.position = editor.ghost_pos;
            o.model_path = editor.selected_model;
            o.behavior = STATIC;
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
    s_del_last = del;

    // save map
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS &&
        glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS){
            world_map_save(map, Const::MAP_SAVE_PATH);
    }
}
