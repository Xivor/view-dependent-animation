#pragma once
#include <map>
#include <glm/glm.hpp>
#include "renderer/ShaderManager.h"
#include "renderer/Types_renderer.h"

enum class PassTag;
class Shader;

class Mesh {
public:
    //Meshes don't have shader Handle but only shader keys
    //shader handles of object of the scene are created at runtime in main.c
    //and stored in the atribute passTagShaderHandle of the objects of the
    //vector "objects" of the scene
    virtual const std::map<PassTag, ShaderKey> shaderKeysMap() const = 0;

    /**
     * @brief gives a point that is representative of the mesh barycentre, meaning there is only one constant translation from the barycentre.
     */
    virtual void draw() const = 0;
    virtual void uploadUniforms(const Shader& shader, const RenderContext& ctx) const;
    virtual void drawMeshUI(int selectedJointId) {}
    virtual ~Mesh() = default;

    Mesh()                         = default;
    Mesh(const Mesh&)              = delete;
    Mesh& operator=(const Mesh&)   = delete;

    virtual int getCurrentFrame();
};
