#include "RiggedMesh.h"
#include "RenderPipeline.h"
#include "cnpy.h"
#include "glm/ext/matrix_transform.hpp"
#include "renderer/AnimatedMesh.h"
#include "renderer/Shader.h"
#include "renderer/TextureLoader.h"
#include <assimp/postprocess.h>
#include <assimp/Importer.hpp>
#include <fstream>


// ####################
// ####################
//      PUBLIC
// ####################
// ####################

RiggedMesh::RiggedMesh(const std::string &objPath, const std::string &jointsPath, const std::string &weightsPath, const std::string& posePath) {
    m_scene = m_importer.ReadFile(objPath,
            aiProcess_CalcTangentSpace|
            aiProcess_Triangulate           |
            aiProcess_JoinIdenticalVertices |
            aiProcess_SortByPType           |
            aiProcess_GenNormals);

    if (!m_scene || !m_scene->HasMeshes()) {
        Log::error("no meshes found in path: " + objPath);
        return;
    }

    parseMeshes();
    setupJoints(jointsPath);
    // setupSkinningWeights(weightsPath);
    // setPose(posePath);

    populateBuffers();
    loadAlbedoTexture(objPath);
}

RiggedMesh::~RiggedMesh() {
  glDeleteBuffers(1, &m_ebo);
  glDeleteBuffers(1, &m_vbo);
  glDeleteVertexArrays(1, &m_vao);
  if (m_textureID) glDeleteTextures(1, &m_textureID);
}

void RiggedMesh::uploadUniforms(const Shader &shader, const RenderContext &ctx) const {
  Mesh::uploadUniforms(shader, ctx);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bones_data_ssbo);
  for (int i = 0; i < m_bonesInfo.size(); ++i) {
    // shader.set(("gBones["+std::to_string(i)+"]").c_str(), m_bonesInfo[i].finalTransformationMatrix * m_bonesInfo[i].offsetMatrix);
    shader.set(("gBones["+std::to_string(i)+"]").c_str(), glm::identity<glm::mat4>());
  }
  shader.set("hasTexture", m_textureID != 0 ? 1 : 0);
  shader.set("albedoTexture", 0);
  if (m_textureID != 0) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textureID);
  }
}

void RiggedMesh::draw() const {
  glBindVertexArray(m_vao);
  glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
}

void RiggedMesh::drawMeshUI(int selectedJointId) {
  if (!m_scene || m_scene->mNumAnimations == 0) {
    ImGui::TextDisabled("No animations.");
    return;
  }
  // ImGui::Text("Joints");
  // ImGui::Separator();
  // for (int i = 0; i < m_bonesInfo.size(); i++) {
  //   glm::vec3 jointGlobalPos = m_bonesInfo[i].finalTransformationMatrix[3];
  //   std::string label = "Joint " + std::to_string(i) + ": " + glm::to_string(jointGlobalPos);
  //   // ImGui::Text("Local Position: %.2f, %.2f, %.2f", jointLocalPos.x, jointLocalPos.y, jointLocalPos.z);
  //   // ImGui::Text("Global Position: %.2f, %.2f, %.2f", jointGlobalPos.x, jointGlobalPos.y, jointGlobalPos.z);
  //   bool selected = (selectedJointId == i);
  //   if (selected) {
  //     ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "%s (Selected)", label.c_str());
  //     // ImGui::Indent();
  //     // ImGui::Unindent();
  //   } else {
  //     ImGui::Text("%s", label.c_str());
  //   }
  // }
}

void RiggedMesh::calculateJointsGlobalTransforms(std::vector<glm::mat4> &transforms, glm::mat4 &parentTransform, int jointIdx) const {
  glm::mat4 localTranslate = glm::inverse(m_bonesInfo[jointIdx].offsetMatrix);
  glm::mat4 localRotate = glm::identity<glm::mat4>();
  if (jointIdx < m_joints_local_rotations.size()) {
    localRotate = m_joints_local_rotations[jointIdx];
  }
  glm::mat4 localTransform = localTranslate * localRotate;
  glm::mat4 globalTransform = parentTransform * localTransform;
  transforms[jointIdx] = m_globalInverseTransform * globalTransform * m_bonesInfo[jointIdx].offsetMatrix;
  m_bonesInfo[jointIdx].finalTransformationMatrix = transforms[jointIdx];
  for (auto &childIdx : m_bonesInfo[jointIdx].children) {
    calculateJointsGlobalTransforms(transforms, globalTransform, childIdx);
  }
}

// ####################
// ####################
//      PROTECTED
// ####################
// ####################

void RiggedMesh::parseMeshes() {
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

void RiggedMesh::setupJoints(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    Log::error("failed to open joints file " + path);
    return;
  }
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::stringstream ss(line);
    int index, parent;
    float x, y, z;
    std::string name;
    if (!(ss >> index >> parent >> x >> y >> z >> name)) {
      continue;
    }
    m_boneNameToIndexMap[name] = index;
    if (name == "RootNode") rootJointIdx = index;
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
    glm::mat4 offsetMatrix = glm::inverse(transform);

    BoneInfo bi(offsetMatrix);
    bi.parent = parent;
    bi.index = index;
    bi.finalTransformationMatrix = transform;
    m_bonesInfo.push_back(bi);
    m_joint_transforms.push_back(transform);
  }
  // m_joint_transforms.resize(m_bonesInfo.size());
  // std::vector<glm::mat4> transforms;
  // transforms.resize(m_bonesInfo.size());
  // glm::mat4 identity = glm::identity<glm::mat4>();
  // calculateJointsGlobalTransforms(transforms, identity, rootJointIdx);
  // for (int i = 0; i < m_bonesInfo.size(); ++i) {
  //   if (m_bonesInfo[i].parent != -1) {
  //     m_bonesInfo[m_bonesInfo[i].parent].children.push_back(m_bonesInfo[i].index);
  //   }
    // m_joint_transforms[i] = transforms[i];
  // }
}

void RiggedMesh::setupSkinningWeights(const std::string &path) {
  cnpy::NpyArray arr = cnpy::npy_load(path);
  float* data = arr.data<float>();
  size_t numVertices = m_vertices.size();
  for (int i = 0; i < numVertices; ++i) {
    for (int j = 0; j < MAX_NUM_BONES_PER_VERTEX; ++j) {
      int boneId = static_cast<int>(data[i * MAX_NUM_BONES_PER_VERTEX * 2 + j]);
      float weight = data[i * MAX_NUM_BONES_PER_VERTEX * 2 + MAX_NUM_BONES_PER_VERTEX + j];
      if (weight > 0.0f) {
        m_vertices[i].addBoneData(boneId, weight);
      }
    }
  }
}

void RiggedMesh::setPose(const std::string &path) {
  cnpy::NpyArray arr = cnpy::npy_load(path);
  float* data = arr.data<float>();
  m_joints_local_rotations.resize(m_bonesInfo.size());
  for (int i = 0; i < m_bonesInfo.size(); i++) {
    glm::mat4 rot = glm::rotate(glm::identity<glm::mat4>(), data[3*i], {1, 0, 0});
    rot = glm::rotate(rot, data[3*i + 1], {0, 1, 0});
    rot = glm::rotate(rot, data[3*i + 2], {0, 0, 1});
    m_joints_local_rotations[i] = rot;
  }
}

void RiggedMesh::populateBuffers() {
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
}

void RiggedMesh::loadAlbedoTexture(const std::string &meshPath) {
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
