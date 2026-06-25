cmake_minimum_required(VERSION 3.20)
project(byahengine VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(byahengine
    src/main.cpp
    src/core/app.cpp
    src/core/window.cpp
    src/core/editor_cam.cpp
    src/core/editor_input.cpp
    src/core/cam.cpp
    src/core/npc_update.cpp
    src/core/settings.cpp
    src/renderer/shader.cpp
    src/renderer/mesh.cpp
    src/renderer/mesh_builder.cpp
    src/renderer/obj_loader.cpp
    src/renderer/obj_mesh.cpp
    src/renderer/editor_renderer.cpp
    src/renderer/font.cpp
    src/renderer/hud.cpp
    src/renderer/scene.cpp
    src/tricycle/tricycle_mesh.cpp
    src/tricycle/tricycle_model.cpp
    src/tricycle/driver_model.cpp
    src/tricycle/driver_anim.cpp
    src/physics/trike_physics.cpp
    src/physics/trike_aabb.cpp
    src/physics/collision.cpp
    src/world/world_map.cpp
    src/world/road_builder.cpp
    src/world/height_field.cpp
    src/world/road_spline.cpp
    src/world/ocean.cpp
    src/world/npc.cpp
    src/world/animal_anim.cpp
    src/world/ambience_zone.cpp
    src/world/rain.cpp
    src/audio/audio.cpp
    vendor/glad/src/glad.c
    src/stb_impl.cpp
)

target_include_directories(byahengine PRIVATE
    src
    vendor/glad/include
    vendor/glfw/include
    vendor/glm
    vendor/stb
    vendor/miniaudio
)

if(WIN32)
    target_link_libraries(byahengine PRIVATE
        ${CMAKE_SOURCE_DIR}/vendor/glfw/lib/libglfw3.a
        opengl32
        gdi32
        user32
        shell32
    )
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(byahengine PRIVATE
        ${CMAKE_SOURCE_DIR}/vendor/glfw/lib/libglfw3.a
        OpenGL::GL
        dl
        pthread
        m
    )
endif()