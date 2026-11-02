#include "Scene.h"
#include "renderer/Mesh.h"

#include "glad/glad.h"

static void ShaderDefinesDisplay(const std::set<std::string>& defines)
{
    if (defines.empty()) {
        ImGui::TextDisabled("No defines.");
        return;
    }

    for (const auto& define : defines) {
        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.f, 1.f), "#define");
        ImGui::SameLine();
        ImGui::Text("%s", define.c_str());
    }
}

// TODO move this function to a more adequate place
void Object::draw(Mesh* mesh, int selectedJointId, bool *p_open) {
    if (!ImGui::Begin("Object Inspector", p_open)) {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", glm::value_ptr(transform.position), 0.1f);
        glm::vec3 euler = glm::degrees(glm::eulerAngles(transform.rotation));
        if (ImGui::DragFloat3("Rotation (deg)", glm::value_ptr(euler), 1.0f))
            transform.rotation = glm::quat(glm::radians(euler));
        ImGui::DragFloat3("Scale",    glm::value_ptr(transform.scale),    0.05f);
    }

    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Couleur",   glm::value_ptr(material.color));
        ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Shader Specifications")) {
        for (const auto& [passTag, shaderKey] : passTagShaderSpecifications) {
            ImGui::PushID(PassTagToString(passTag).data());

            if (ImGui::TreeNode(("Pass Tag : " + PassTagToString(passTag)).c_str())) {
                if (shaderKey.isCompute()) {
                    ImGui::LabelText("Compute", "%s", shaderKey.comp.c_str());
                } else {
                    ImGui::LabelText("Vertex",   "%s", shaderKey.vert.c_str());
                    ImGui::LabelText("Fragment", "%s", shaderKey.frag.c_str());
                    if (!shaderKey.geom.empty()) ImGui::LabelText("Geometry",     "%s", shaderKey.geom.c_str());
                    if (!shaderKey.tesc.empty()) ImGui::LabelText("Tess Control", "%s", shaderKey.tesc.c_str());
                    if (!shaderKey.tese.empty()) ImGui::LabelText("Tess Eval",    "%s", shaderKey.tese.c_str());
                }

                ImGui::Separator();
                ImGui::Text("Defines");
                ShaderDefinesDisplay(shaderKey.defines);

                ImGui::TreePop();
            }

            ImGui::PopID();
        }
    }

    if (mesh && ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
        mesh->drawMeshUI(selectedJointId);
    }

    ImGui::End();
}

void Scene::setup() {
    if (!m_skybox) {
        Log::error("No skybox defined");
    }
    glCreateBuffers(1, &lights_ssbo);
    glNamedBufferData(lights_ssbo, lights.size() * sizeof(Light), lights.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, lights_ssbo);

}
