#include "editor_cam.hpp"
#include "const.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

// last known mouse pos
static double s_last_mouse_x = 0.0;
static double s_last_mouse_y = 0.0;
static bool s_first_drag = true; // prevents jump on first RMB press

void editor_cam_init(GLFWwindow* window){
    glfwGetCursorPos(window, &s_last_mouse_x, &s_last_mouse_y);
    s_first_drag = true;
}

void editor_cam_update(EditorState& editor, GLFWwindow* window, float dt){
    bool rmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);

    // only rotate when RMB is held
    if (rmb){
        if (s_first_drag){
            // swallow first frame so cam does not snap on initial click
            s_last_mouse_x = mx;
            s_last_mouse_y = my;
            s_first_drag = false;
        }

        float dx = (float)(mx- s_last_mouse_x);
        float dy = (float)(my- s_last_mouse_y);

        editor.cam_yaw -= dx * Const::EDITOR_LOOK_SENSITIVITY;
        editor.cam_pitch -= dy * Const::EDITOR_LOOK_SENSITIVITY;

        // clamp pitch so cam never flips over goofy
        editor.cam_pitch = std::clamp(editor.cam_pitch, 
        -glm::half_pi<float>() + 0.05f,glm::half_pi<float>() - 0.05f);
    }
    else{
        // reset first drag flag when RMB released
        s_first_drag = true;
    }

    s_last_mouse_x = mx;
    s_last_mouse_y = my;

    // build look dir from yaw + pitch
    glm::vec3 forward = {
        std::cos(editor.cam_pitch) * std::sin(editor.cam_yaw),
        std::sin(editor.cam_pitch),
        std::cos(editor.cam_pitch) * std::cos(editor.cam_yaw)
    };

    glm::vec3 world_up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(forward, world_up));
    
    // WASD movement along freecam axes
    float speed = Const::EDITOR_CAM_SPEED * dt;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) editor.cam_pos += forward * speed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) editor.cam_pos -= forward * speed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) editor.cam_pos -= right * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) editor.cam_pos += right * speed;

    // vertical up/down
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) editor.cam_pos += world_up * speed;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) editor.cam_pos -= world_up * speed;

    // arrow key look
    // note that it only fires when no object is selected
    if (editor.selected_id == -1 && editor.mode != MODE_LIGHT && editor.mode != MODE_POSE){
        float look = Const::EDITOR_LOOK_SENSITIVITY * 600.0f * dt;
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) editor.cam_yaw += look;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) editor.cam_yaw -= look;
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) editor.cam_pitch += look;
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) editor.cam_pitch -= look;

        editor.cam_pitch = std::clamp(editor.cam_pitch,
            -glm::half_pi<float>() + 0.05f,
             glm::half_pi<float>() - 0.05f);
    }
}

glm::mat4 editor_cam_get_view(const EditorState& editor){
    glm::vec3 forward = {
        std::cos(editor.cam_pitch) * std::sin(editor.cam_yaw),
        std::sin(editor.cam_pitch),
        std::cos(editor.cam_pitch) * std::cos(editor.cam_yaw)
    };
    return glm::lookAt(editor.cam_pos, editor.cam_pos + forward, glm::vec3(0,1,0));
}