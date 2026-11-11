set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
cmake_minimum_required(VERSION 3.11 FATAL_ERROR)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(stylize-animation CXX)

#===========================================================================================
# OPTIONAL LIBRARiES
#
# The following libraries can be pulled down and added to the project by turnning the
# desired options on. Open an issue for additional default support.
# See: https://github.com/Hoshiningen/OpenGL-Template/issues

set(ENABLE_ASSIMP   ON CACHE BOOL "Add Open Asset Import Library (assimp) to the project" FORCE)
set(ENABLE_IMGUI    ON CACHE BOOL "Add Dear ImGui to the project" FORCE)
set(ENABLE_CNPY     ON CACHE BOOL "Add cnpy for reading .npy files to the project" FORCE)

#===========================================================================================
# GLAD CONFIGURATION
#
# Configure GLAD loader generation variables (lines below pulled from the GLAD top-level
# CMakeLists.txt file, with slight modifications.
# Refer to https://github.com/Dav1dde/glad for more information)

set(GLAD_OUT_DIR        "${CMAKE_CURRENT_BINARY_DIR}" CACHE STRING "Output directory" FORCE)
set(GLAD_PROFILE        "core" CACHE STRING "OpenGL profile" FORCE)
set(GLAD_API            "gl=4.6" CACHE STRING "API type/version pairs, like \"gl=3.2,gles=\", no version means latest" FORCE)
set(GLAD_GENERATOR      "c" CACHE STRING "Language to generate the binding for" FORCE)
set(GLAD_EXTENSIONS     "" CACHE STRING "Path to extensions file or comma separated list of extensions, if missing all extensions are included" FORCE)
set(GLAD_SPEC           "gl" CACHE STRING "Name of the spec" FORCE)
set(GLAD_ALL_EXTENSIONS OFF CACHE BOOL "Include all extensions instead of those specified by GLAD_EXTENSIONS" FORCE)
set(GLAD_NO_LOADER      OFF CACHE BOOL "No loader" FORCE)
set(GLAD_REPRODUCIBLE   OFF CACHE BOOL "Reproducible build" FORCE)

#===========================================================================================

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Set directory paths
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(sources
    ${SOURCE_DIR}/main.cpp
    ${SOURCE_DIR}/core/Log.cpp
    ${SOURCE_DIR}/core/Window.cpp
    ${SOURCE_DIR}/core/AssetManager.cpp
    ${SOURCE_DIR}/scene/Transform.cpp
    ${SOURCE_DIR}/scene/Camera.cpp
    ${SOURCE_DIR}/scene/CameraController.cpp
    ${SOURCE_DIR}/scene/Cinematic.cpp
    ${SOURCE_DIR}/renderer/Mesh.cpp
    ${SOURCE_DIR}/renderer/Shader.cpp
    ${SOURCE_DIR}/renderer/StaticMesh.cpp
    ${SOURCE_DIR}/renderer/Geometry.cpp
    ${SOURCE_DIR}/renderer/Renderer.cpp
    ${SOURCE_DIR}/renderer/ShaderManager.cpp
    ${SOURCE_DIR}/renderer/TextureLoader.cpp
    ${SOURCE_DIR}/ui/DebugConsole.cpp
    ${SOURCE_DIR}/ui/RenderStats.cpp
    ${SOURCE_DIR}/renderer/AnimatedMesh.cpp
    ${SOURCE_DIR}/renderer/SmearMesh.cpp
    ${SOURCE_DIR}/core/Timer.cpp
    ${SOURCE_DIR}/core/Timer.h
    ${SOURCE_DIR}/ui/SelectionManager.cpp
    ${SOURCE_DIR}/ui/SelectionManager.h
    ${SOURCE_DIR}/renderer/RenderPipeline.cpp
    ${SOURCE_DIR}/renderer/RenderPipeline.h
    ${SOURCE_DIR}/renderer/RiggedMesh.cpp
    ${SOURCE_DIR}/renderer/RiggedMesh.h
    src/core/MegaWindowContext.h
    src/scene/Scene.cpp
)

add_executable(stylize-animation ${sources}) # ${includes}

target_include_directories(stylize-animation PRIVATE ${SOURCE_DIR})

# Perform dependency linkage
include(${CMAKE_DIR}/LinkGLFW.cmake)
LinkGLFW(stylize-animation PRIVATE)

include(${CMAKE_DIR}/LinkGLAD.cmake)
LinkGLAD(stylize-animation PRIVATE)

find_package(OpenGL REQUIRED)
target_link_libraries(stylize-animation PRIVATE OpenGL::GL)

# Handle the optional libraries
if (${ENABLE_ASSIMP})
  set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    include(${CMAKE_DIR}/LinkASSIMP.cmake)
    LinkASSIMP(stylize-animation PRIVATE)
endif()

if (${ENABLE_IMGUI})
    include(${CMAKE_DIR}/LinkImGui.cmake)
    LinkImGui(stylize-animation PRIVATE)
endif()

if (${ENABLE_CNPY})
  include(${CMAKE_DIR}/LinkCNPY.cmake)
  LinkCNPY(stylize-animation PRIVATE)
endif()

include(${CMAKE_DIR}/LinkGLM.cmake)
LinkGLM(stylize-animation PRIVATE)

include(${CMAKE_DIR}/LinkCXXOPTS.cmake)
LinkCXXOPTS(stylize-animation PRIVATE)

include(${CMAKE_DIR}/LinkSTB.cmake)
LinkSTB(stylize-animation PRIVATE)

#copy target to root
add_custom_command(TARGET ${PROJECT_NAME}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_CURRENT_SOURCE_DIR})

# Enable C++17
set_target_properties(stylize-animation PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)

# Set project folders
set_target_properties(stylize-animation PROPERTIES FOLDER ${PROJECT_NAME})

# Set compile options
# target_compile_options(stylize-animation PRIVATE -O3)
