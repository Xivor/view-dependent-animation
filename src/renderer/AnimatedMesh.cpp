#include "renderer/AnimatedMesh.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "renderer/Shader.h"
#include "renderer/ShaderManager.h"
#include "renderer/TextureLoader.h"
#include "core/Log.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <cstring>
#include <glm/gtx/quaternion.hpp>
#include <functional>
#include <imgui.h>

#include "RenderPipeline.h"

#define DEBUG

// TODO globally change "node" for "bone" if the node is guaranteed to be a
// bone. Lots of headaches because of that uncertainty.

AnimatedMesh::AnimatedMesh(const std::string& path) {
    m_scene = m_importer.ReadFile(path,
            aiProcess_CalcTangentSpace|
            aiProcess_Triangulate           |
            aiProcess_JoinIdenticalVertices |
            aiProcess_SortByPType           |
            aiProcess_GenNormals);

    if (!m_scene) {
        Log::error(m_importer.GetErrorString());
        return;
    }
    if (!m_scene->HasMeshes()) {
        Log::error("no meshes found in path: " + path);
        return;
    }
    double anim_duration = m_scene->mAnimations[0]->mDuration;
    printf("anim duration : %f\n",anim_duration );

    parseMeshes();
    buildNodeMap(m_scene->mRootNode, 0);
    populateBuffers();
    loadAlbedoTexture(path);
}

AnimatedMesh::~AnimatedMesh() {
    glDeleteBuffers(1, &m_ebo);
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
    if (m_textureID) glDeleteTextures(1, &m_textureID);
}

void AnimatedMesh::draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES,m_indexCount,GL_UNSIGNED_INT,nullptr);
}

void AnimatedMesh::uploadUniforms(const Shader& shader, const RenderContext& ctx) const {
    Mesh::uploadUniforms(shader,ctx);
    std::vector<glm::mat4> transforms;
    getBoneTransforms(transforms);
    for (unsigned int i = 0; i < m_bonesInfo.size(); i++) {
      m_bonesInfo[i].finalTransformationMatrix = transforms[i];
    }
    for (unsigned int i = 0; i < m_bonesInfo.size(); i++) {
      transforms[i] *= m_bonesInfo[i].offsetMatrix;
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bones_data_ssbo);
    for (int i = 0 ; i < m_bonesInfo.size(); ++i) {
        shader.set(("gBones["+std::to_string(i)+"]").c_str(), transforms[i]);
    }

    shader.set("hasTexture", m_textureID != 0 ? 1 : 0);
    shader.set("albedoTexture", 0);
    if (m_textureID != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_textureID);
    }
}

// This method adds a bone to the dictionary if it isn't already there, then
// returns an ID. It only stores bones (and not dummy nodes) because it receives
// a proper aiBone*
int AnimatedMesh::getBoneID(const aiBone* bone) {
    std::string boneName(bone->mName.data);
    auto it = m_boneNameToIndexMap.find(boneName);
    if (it == m_boneNameToIndexMap.end()) {
        int id = static_cast<int>(m_boneNameToIndexMap.size());
        m_boneNameToIndexMap[boneName] = id;
        return id;
    }
    return it->second;
}

glm::mat4 AnimatedMesh::calculateInterpolatedRotation(const double& animationTicks, const aiNodeAnim* animationNode) {
    if (animationNode->mNumRotationKeys == 1) {
        const auto& q = animationNode->mRotationKeys[0].mValue;
        glm::quat out = glm::quat(q.w, q.x, q.y, q.z);
        return glm::toMat4(glm::normalize(out));
    }
    int rotation_index = 0;
    for (unsigned int j = 0; j < animationNode->mNumRotationKeys - 1; ++j) {
        double t = animationNode->mRotationKeys[j + 1].mTime;
        if (animationTicks < t) {
            rotation_index = j;
            break;
        }
    }
    int next_rotation_index = rotation_index + 1;
    assert(next_rotation_index < animationNode->mNumRotationKeys);
    double t1 = animationNode->mRotationKeys[rotation_index].mTime;
    double t2 = animationNode->mRotationKeys[next_rotation_index].mTime;
    double delta_time = t2 - t1;
    double factor = (animationTicks-t1) / delta_time;
    const aiQuaternion& start = animationNode->mRotationKeys[rotation_index].mValue;
    const aiQuaternion& end = animationNode->mRotationKeys[next_rotation_index].mValue;
    aiQuaternion out;
    aiQuaternion::Interpolate(out,start,end,static_cast<float>(factor));
    out.Normalize();
    return glm::toMat4(glm::quat(out.w, out.x, out.y, out.z));
}

glm::mat4 AnimatedMesh::calculateInterpolatedPosition(const double& animationTicks, const aiNodeAnim* animationNode) {
    if (animationNode->mNumPositionKeys == 1) {
        glm::vec3 out = glm::make_vec3(&animationNode->mPositionKeys[0].mValue.x);
        return glm::translate(glm::mat4(1.0f), out);
    }
    int position_index = 0;
    for (unsigned int j = 0; j < animationNode->mNumPositionKeys - 1; ++j) {
        double t = animationNode->mPositionKeys[j + 1].mTime;
        if (animationTicks < t) {
            position_index = j;
            break;
        }
    }
    int next_position_index = position_index + 1;
    assert(next_position_index < animationNode->mNumPositionKeys);
    double t1 = animationNode->mPositionKeys[position_index].mTime;
    double t2 = animationNode->mPositionKeys[next_position_index].mTime;
    double delta_time = t2 - t1;
    double factor = (animationTicks-t1) / delta_time;
    const aiVector3D& start = animationNode->mPositionKeys[position_index].mValue;
    const aiVector3D& end = animationNode->mPositionKeys[next_position_index].mValue;
    aiVector3D delta = end - start;
    glm::vec3 out = glm::make_vec3(&start.x) + ((float)factor)*glm::make_vec3(&delta.x);
    return glm::translate(glm::mat4(1.0f), out);
}

glm::mat4 AnimatedMesh::calculateInterpolatedScale(const double& animationTicks, const aiNodeAnim* animationNode) {

    if (animationNode->mNumScalingKeys == 1) {
        glm::vec3 out = glm::make_vec3(&animationNode->mScalingKeys[0].mValue.x);
        return glm::scale(glm::mat4(1.0f), out);
    }
    int scale_index = 0;
    for (unsigned int j = 0; j < animationNode->mNumScalingKeys - 1; ++j) {
        double t = animationNode->mScalingKeys[j + 1].mTime;
        if (animationTicks < t) {
            scale_index = j;
            break;
        }
    }
    int next_scale_index = scale_index + 1;
    assert(next_scale_index < animationNode->mNumScalingKeys);
    double t1 = animationNode->mScalingKeys[scale_index].mTime;
    double t2 = animationNode->mScalingKeys[next_scale_index].mTime;
    double delta_time = t2 - t1;
    double factor = (animationTicks-t1) / delta_time;
    const aiVector3D& start = animationNode->mScalingKeys[scale_index].mValue;
    const aiVector3D& end = animationNode->mScalingKeys[next_scale_index].mValue;
    aiVector3D delta = end - start;
    glm::vec3 out = glm::make_vec3(&start.x) + ((float)factor)*glm::make_vec3(&delta.x);
    return glm::scale(glm::mat4(1.0f), out);
}

const aiNodeAnim* AnimatedMesh::getNodeAnimFromNode(const aiNode* node) const {
  std::string nodeName = node->mName.data;

  const aiAnimation* animation = m_scene->mAnimations[m_currentAnimIndex];
  for (unsigned int i = 0; i < animation->mNumChannels; i++) {
    const aiNodeAnim* animation_node_i = animation->mChannels[i];
    if (std::string(animation_node_i->mNodeName.data) == nodeName) {
      return animation_node_i;
    }
  }
  return nullptr;
}

void AnimatedMesh::buildNodeMap(const aiNode *node, int h) {
  if (!node) return;
  m_nodeMap[node->mName.C_Str()] = node;
  std::cout << "height = " << h << ": " << node->mName.C_Str() << std::endl;
  for (int i = 0; i < node->mNumChildren; ++i) {
    buildNodeMap(node->mChildren[i], h+1);
  }
}

// recursive function that calculates the bones global transforms at a given
// moment in the animation's timeline. Since the offset matrix of the bones is
// constant, we do not need to return them.
void AnimatedMesh::calculateGlobalTransformsAtTick(
    const double &animationTicks, const aiNode *node,
    const glm::mat4 &parentTransformation,
    std::vector<glm::mat4> &outBoneGlobalTransform
) const {
  glm::mat4 node_transformation = aiMat4ToGlmMat4(node->mTransformation);
  std::string nodeName = node->mName.data;
  const aiNodeAnim *animation_node = getNodeAnimFromNode(node);

  if (animation_node) {
    glm::mat4 translation = calculateInterpolatedPosition(animationTicks, animation_node);
          glm::mat4 rotation = calculateInterpolatedRotation(animationTicks, animation_node);
        glm::mat4 scale = calculateInterpolatedScale(animationTicks, animation_node);
        node_transformation=translation*rotation*scale;
    }

    glm::mat4 globalTransformation = parentTransformation*node_transformation;

    if (m_boneNameToIndexMap.count(nodeName) != 0) {
        auto idx = m_boneNameToIndexMap[nodeName];
        outBoneGlobalTransform[idx] = m_globalInverseTransform * globalTransformation;
    }
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        calculateGlobalTransformsAtTick(animationTicks,node->mChildren[i], globalTransformation, outBoneGlobalTransform);
    }
}

void AnimatedMesh::getBoneTransforms(std::vector<glm::mat4> &transforms) const {
    if (m_scene->HasAnimations()) {
        double time = timer.getCurrentValue();
        const aiAnimation* anim = m_scene->mAnimations[m_currentAnimIndex];
        double ticks_per_second = anim->mTicksPerSecond != 0 ? anim->mTicksPerSecond : 25.0;
        double ticks = fmod(time * ticks_per_second, anim->mDuration);
        transforms.resize(m_bonesInfo.size());

        auto identity = glm::identity<glm::mat4>();

        calculateGlobalTransformsAtTick(ticks, m_scene->mRootNode, identity, transforms);
        // for (unsigned int i = 0; i < m_bonesInfo.size(); i++) {
        //     transforms[i] *= m_bonesInfo[i].offsetMatrix; // BUG if something explodes look here
        // }
    }
    else {
        transforms.resize(m_bonesInfo.size());

        auto identity = glm::identity<glm::mat4>();

        calculateGlobalTransformsAtTick(0, m_scene->mRootNode, identity, transforms);
        // for (unsigned int i = 0; i < m_bonesInfo.size(); i++) {
        //     transforms[i] *= m_bonesInfo[i].offsetMatrix;
        // }
    }
}

void AnimatedMesh::parseMeshes() {
    m_globalInverseTransform = glm::inverse(aiMat4ToGlmMat4(m_scene->mRootNode->mTransformation));
    unsigned int baseVertex = 0;
    Log::info("scene " + (!std::string(m_scene->mName.data).empty() ? std::string(m_scene->mName.data) : "Untitled") + " : ");
    unsigned int count = 0;
    for (unsigned int i = 0; i < m_scene->mNumMeshes; i++) {
        const aiMesh* mesh = m_scene->mMeshes[i];
        Log::info("mesh " + std::string(mesh->mName.data) + " : ");
        if (mesh->HasPositions() && mesh->HasNormals()) {
            Log::info(" - has " + std::to_string(mesh->mNumVertices) + " vertices");
            for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
                const aiVector3D& p = mesh->mVertices[j];
                const aiVector3D& n = mesh->mNormals[j];
                glm::vec2 uv{0.f};
                if (mesh->HasTextureCoords(0))
                    uv = {mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y};
                AnimatedVertex v;
                v.position = glm::vec3(p.x, p.y, p.z);
                v.normal   = glm::vec3(n.x, n.y, n.z);
                v.uv       = uv;
                m_vertices.push_back(v);
            }
        }
        else {
            Log::info(" - no mesh found");
        }

        if (mesh->HasBones()) {
            Log::info(" - has " + std::to_string(mesh->mNumBones) + " bones");
            for (unsigned int b = 0; b < mesh->mNumBones; b++) {
                const aiBone* bone = mesh->mBones[b];
                int boneID = getBoneID(bone);
                if (boneID == m_bonesInfo.size()) {
                    BoneInfo bi{aiMat4ToGlmMat4(bone->mOffsetMatrix)};
                    m_bonesInfo.push_back(bi);
                }
                for (unsigned int w = 0; w < bone->mNumWeights; w++) {
                    unsigned int globalVertexID = baseVertex + bone->mWeights[w].mVertexId;
                    m_vertices[globalVertexID].addBoneData(boneID, bone->mWeights[w].mWeight);
                }
                Log::info("   - bone " + std::to_string(boneID) + " influences " + std::to_string(mesh->mNumVertices) + " vertices");
            }
            m_joints_world_pos.resize(mesh->mNumBones);
        }
        else {
            Log::info(" - no bones (cannot be animated !)");
        }

        if (mesh->HasFaces()) {
            Log::info(" - has " + std::to_string(mesh->mNumFaces) + " faces");
            for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
                const aiFace& face = mesh->mFaces[j];
                assert(face.mNumIndices == 3);
                m_indices.push_back(baseVertex + face.mIndices[0]);
                m_indices.push_back(baseVertex + face.mIndices[1]);
                m_indices.push_back(baseVertex + face.mIndices[2]);
            }
        }
        else {
            Log::info(" - no faces");
        }
        baseVertex += mesh->mNumVertices;
    }
    m_indexCount = static_cast<GLsizei>(m_indices.size());
}

void AnimatedMesh::populateBuffers() {

    glCreateVertexArrays(1, &m_vao);
    glCreateBuffers(1, &m_vbo);
    glCreateBuffers(1, &m_ebo);

    glNamedBufferData(m_vbo,m_vertices.size()*sizeof(AnimatedVertex),m_vertices.data(),GL_STATIC_DRAW);
    glNamedBufferData(m_ebo,m_indices.size()* sizeof(uint32_t),m_indices.data(),GL_STATIC_DRAW);

    glVertexArrayVertexBuffer (m_vao,0,m_vbo,0,sizeof(AnimatedVertex));
    glVertexArrayElementBuffer(m_vao,m_ebo);

    glEnableVertexArrayAttrib (m_vao,POSITION_LOCATION);
    glVertexArrayAttribFormat (m_vao,POSITION_LOCATION,3,GL_FLOAT,GL_FALSE,offsetof(AnimatedVertex, position));
    glVertexArrayAttribBinding(m_vao,POSITION_LOCATION,0);

    glEnableVertexArrayAttrib (m_vao,NORMAL_LOCATION);
    glVertexArrayAttribFormat (m_vao,NORMAL_LOCATION,3,GL_FLOAT,GL_FALSE,offsetof(AnimatedVertex, normal));
    glVertexArrayAttribBinding(m_vao,NORMAL_LOCATION,0);

    glEnableVertexArrayAttrib (m_vao, UV_LOCATION);
    glVertexArrayAttribFormat (m_vao, UV_LOCATION, 2, GL_FLOAT, GL_FALSE, offsetof(AnimatedVertex, uv));
    glVertexArrayAttribBinding(m_vao, UV_LOCATION, 0);

    struct VertexBoneData {
        unsigned int ids[MAX_NUM_BONES_PER_VERTEX];
        float weights[MAX_NUM_BONES_PER_VERTEX];
    };

    auto allBones = std::vector<VertexBoneData>(m_vertices.size(),VertexBoneData());

    for (int i = 0 ; i < m_vertices.size(); i++) {
        // fill boneIDs
        auto bone = VertexBoneData{};
        for (int j = 0; j < MAX_NUM_BONES_PER_VERTEX; j++) {
            bone.ids[j] = m_vertices[i].bonesIDs[j];
            bone.weights[j] = m_vertices[i].weights[j];
        }
        allBones[i] = bone;
    }

    glCreateBuffers(1, &bones_data_ssbo);
    glNamedBufferData(bones_data_ssbo, allBones.size() * sizeof(VertexBoneData), allBones.data(), GL_STATIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bones_data_ssbo);

    timer.start();
}

void AnimatedMesh::setTimer(const double& minTime, const double& maxTime) {
    this->timer = Timer(minTime,maxTime);
}

void AnimatedMesh::getBonePosition(const aiNode *currentNode,
                                   const std::vector<glm::mat4> &transforms,
                                   glm::vec3 &c_r,
                                   glm::vec3 &c_t) {
    auto getGlobalTransform = [&](const std::string &name) -> glm::mat4 {
        auto it = m_boneNameToIndexMap.find(name);
        if (it != m_boneNameToIndexMap.end()) return transforms[it->second];
        return glm::mat4(1.0f);
    };

    c_r = glm::vec3(getGlobalTransform(currentNode->mName.C_Str())[3]);

    std::vector<const aiNode*> childrenBones;
    getChildrenBones(currentNode, childrenBones);

    if (!childrenBones.empty()) {
        glm::vec3 avg_tip(0.0f);
        for (const aiNode* child : childrenBones) {
            avg_tip += glm::vec3(getGlobalTransform(child->mName.C_Str())[3]);
        }
        c_t = avg_tip / static_cast<float>(childrenBones.size());
    } else {
        const aiNode* parentBone = getParentBone(currentNode);
        if (parentBone) {
            glm::vec3 parent_pos = glm::vec3(getGlobalTransform(parentBone->mName.C_Str())[3]);
            glm::vec3 direction = c_r - parent_pos;
            c_t = c_r + direction; // Make the leaf bone exactly as long as its parent, pointing the same way
        } else {
            c_t = c_r + glm::vec3(0.0f, 0.1f, 0.0f);
        }
    }
}

void AnimatedMesh::getChildrenBones(const aiNode *currentNode,
                                    std::vector<const aiNode*> &outChildrenBones) {
  // from the current node, go down the tree until you find the first real bone
  // from each immediate children of it

  std::function<const aiNode*(const aiNode*)> getFirstBoneInSubtree = [&](const aiNode *root) -> const aiNode* {
    // can return root itself
    if (m_boneNameToIndexMap.count(root->mName.C_Str()) > 0) return root;
    if (root->mNumChildren == 0) return nullptr;
    for (int i = 0; i < root->mNumChildren; ++i) {
      const aiNode* childBone = getFirstBoneInSubtree(root->mChildren[i]);
      if (childBone != nullptr) {
        return childBone;
      }
    }
    return nullptr;
  };

  for (int i = 0; i < currentNode->mNumChildren; ++i) {
    const aiNode* child = currentNode->mChildren[i];
    const aiNode* bone = getFirstBoneInSubtree(child);
    if (bone != nullptr) {
      outChildrenBones.push_back(bone);
    }
  }
}

const aiNode *AnimatedMesh::getParentBone(const aiNode *currentNode) {
  const aiNode* parent = currentNode->mParent;
  // parent exists but is not bone
  while (parent && m_boneNameToIndexMap.count(parent->mName.C_Str()) <= 0) {
    parent = parent->mParent;
  }
  return parent;
}

void AnimatedMesh::loadAlbedoTexture(const std::string& meshPath) {
    if (!m_scene || m_scene->mNumMaterials == 0) return;

    std::string dir = meshPath.substr(0, meshPath.find_last_of("/\\"));

    for (unsigned int i = 0; i < m_scene->mNumMaterials && m_textureID == 0; i++) {
        const aiMaterial* mat = m_scene->mMaterials[i];
        aiString texPath;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) != AI_SUCCESS) continue;

        std::string texStr = texPath.C_Str();

        if (!texStr.empty() && texStr[0] == '*') {
            // Embedded texture — stored by index inside the FBX
            int idx = std::stoi(texStr.substr(1));
            if (idx < 0 || idx >= static_cast<int>(m_scene->mNumTextures)) continue;
            const aiTexture* emb = m_scene->mTextures[idx];
            if (emb->mHeight == 0) {
                // Compressed blob (PNG/JPG/…) – mWidth is the byte count
                m_textureID = TextureLoader::loadFromMemory(
                    reinterpret_cast<const unsigned char*>(emb->pcData),
                    static_cast<int>(emb->mWidth));
            }
        } else {
            // External file relative to the mesh directory
            std::string fullPath = dir + "/" + texStr;
            m_textureID = TextureLoader::loadFromFile(fullPath);
        }

        if (m_textureID != 0)
            Log::info("AnimatedMesh: loaded albedo texture from '" + texStr + "'");
        else
            Log::warn("AnimatedMesh: could not load texture '" + texStr + "'");
    }
}

void AnimatedMesh::drawMeshUI() {
    if (!m_scene || m_scene->mNumAnimations == 0) {
        ImGui::TextDisabled("No animations.");
        return;
    }

    ImGui::Text("Animations");
    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(m_scene->mNumAnimations); i++) {
        const aiAnimation* anim = m_scene->mAnimations[i];
        std::string label = std::string(anim->mName.C_Str());
        if (label.empty()) label = "Animation " + std::to_string(i);
        bool selected = (m_currentAnimIndex == i);
        if (ImGui::Selectable(label.c_str(), selected)) {
            m_currentAnimIndex = i;
            setTimer(0.0, 100000.0);
            timer.start();
        }
        if (selected) ImGui::SetItemDefaultFocus();
    }
}
