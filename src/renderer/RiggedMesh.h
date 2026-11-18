#pragma once
#include "renderer/Mesh.h"
#include "renderer/AnimatedMesh.h"
#include "renderer/RenderPipeline.h"


// TODO this class and AnimatedMesh have some DISGUSTING code duplication. This ends up as technical debt for solving later.
// NOTE throughout the code for this class, the words "joint" and "bone" come up, but they actually mean the same thing
// BUG there seems to have some kind of mismatch between the input files, so the mesh ends up not rendering well
class RiggedMesh : public Mesh {
  public:
    explicit RiggedMesh(const std::string& objPath, const std::string& jointsPath, const std::string& weightsPath, const std::string& posePath);
    ~RiggedMesh() override;
    void uploadUniforms(const Shader& shader, const RenderContext& ctx) const override;
    void calculateJointsGlobalTransforms(std::vector<glm::mat4>& transforms, glm::mat4 &parentTransform, int jointIdx) const;
    void draw() const override;
    void drawMeshUI(int selectedJointId) override;

    std::vector<glm::mat4> m_joint_transforms =
        {}; // TODO replace all uses of m_joints_world_pos for this variable, as
            // we still get the transforms from it.
            // NOTE ALSO do we really need that when we have the offsetMatrix
            // for m_bonesInfo? -> my theory is that actually offsetMatrix is
            // just a translation for the bone (local space), and m_joint_transforms may be the final world transformation

  protected:
    const aiScene *m_scene = nullptr;
    Assimp::Importer m_importer;
    std::vector<AnimatedVertex> m_vertices = {};
    std::vector<uint32_t> m_indices = {};
    std::vector<glm::mat4> m_joints_local_rotations = {};
    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    GLuint bones_data_ssbo = 0;
    GLuint m_textureID = 0;
    GLsizei m_indexCount = 0;
    glm::mat4 m_globalInverseTransform;
    mutable std::map<std::string, int> m_boneNameToIndexMap; // NOTE here you have the list of bone names and idx
    mutable std::vector<BoneInfo> m_bonesInfo; // NOTE can do m_bonesInfo[boneIdx]
    int rootJointIdx = -1;


    void parseMeshes();
    void setupJoints(const std::string& path);
    void setupSkinningWeights(const std::string& path);
    void setPose(const std::string& path);
    void populateBuffers();
    void loadAlbedoTexture(const std::string& meshPath); // TODO is this needed??


    const std::map<PassTag, ShaderKey> shaderKeysMap() const override {
      // TODO see if animated_mesh.vert actually works for a non-animated rigged mesh
      // TODO using static_mesh.vert temporarily
      std::map<PassTag, ShaderKey> key_map = {{PassTag::Hybrid, ShaderKey{"rigged_mesh.vert", "hybrid.frag"}}};
      return key_map;
    }
};
