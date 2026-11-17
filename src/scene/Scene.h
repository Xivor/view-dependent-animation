/**
 * @file Scene.h
 * @brief Scene container: camera, lights, and all renderable objects.
 */
#pragma once
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Transform.h"
#include "core/Types.h"
#include <map>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "renderer/RenderPipeline.h"
#include "renderer/ShaderManager.h"

class Mesh;


/**
 * @brief Surface appearance properties for an object.
 */
struct Material {
    glm::vec3 color     {1.f};
    float     roughness = 0.5f;
};

enum class PassTag;
using ShaderHandle = uint32_t;
/**
 * @brief A mesh instance in the scene with its own transform and material.
 */
struct Object {
    MeshHandle meshHandle = 0; ///< Reference to geometry in the AssetManager.
    Transform  transform;
    Material   material;
    std::map<PassTag, ShaderKey>    passTagShaderSpecifications;
    std::map<PassTag, ShaderHandle> passTagShaderHandle; // don't touch, filled up at runtime from passTagShaderSpecifications in main.c with ShaderManager.load()
    explicit Object(const std::map<PassTag, ShaderKey>& passTagSpecifications) : passTagShaderSpecifications(passTagSpecifications) {}

    void draw(Mesh* mesh = nullptr, int selectedJointId = -1, bool* p_open = nullptr);
};

/**
 * @brief Top-level scene graph: owns the camera, all lights, and all objects.
 *
 * The Renderer reads from the Scene; it never writes to it.
 */
struct Scene {
    Camera main_camera;
    std::vector<Light>  lights;
    std::vector<Object> objects;
    Object* m_skybox = nullptr;
    unsigned int lights_ssbo = 0;
    /**
     * @brief Sets up the other objects of the scene, typically the lights.
     */
    void setup();
};
