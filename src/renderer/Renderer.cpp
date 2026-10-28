#include "renderer/Renderer.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "renderer/AnimatedMesh.h"
#include "renderer/ShaderManager.h"
#include "renderer/Mesh.h"
#include "renderer/Shader.h"
#include <glad/glad.h>
#include <map>
#include "renderer/SmearMesh.h"
#include "scene/Scene.h"
#include "RenderPipeline.h"

Renderer::Renderer() {
    glEnable(GL_DEPTH_TEST);

}

void Renderer::setup(Scene *scene, const Window *window, const AssetManager &assets, float aspect) {

    //create screen_mesh (rect that takes all the screen)
    glm::vec3 a = {1.0, 1.0, 0.0};
    std::vector<StaticVertex> vertices = {
        {{-1.0, -1.0, 0.0}, {0.0, 0.0, 1.0}}, //pos, normal
        {{-1.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
        {{1.0, -1.0, 0.0}, {0.0, 0.0, 1.0}},
        {{1.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
    };
    std::vector<uint32_t> indices = {0, 2, 1, 2, 3, 1}; //triangles
    p_screen_mesh = std::make_unique<StaticMesh>(vertices, indices); //instancie le mesh

    // Creating the rendering pipeline and the renderer
    m_renderPipeline = RenderPipeline();
    m_renderPipeline.addPass(std::make_shared<MeshIDRenderPass>());
    m_renderPipeline.addPass(std::make_shared<FinalRenderPass>());
    m_renderPipeline.addPass(std::make_shared<HybridRenderPass>());
    m_renderPipeline.addPass(std::make_shared<KAFRenderPass>());
    // Setting up the rendering pipeline and the renderer
    int w, h;
    window->getSize(w,h);
    m_renderPipeline.setup(w,h);
    // Setup the scene (especially insert the lights into the rendering pipeline)
    scene->setup();
}

// TODO each time we add a shader we introduce a block of code in this
// monolithic function. Transform this function to be a
// bunch of verifications (if there is a handler x...) and then call a function
// just to bind values to that specific shader.
// That way, introducing a shader in our project would be just a matter of
// adding that verification / function call here, and adding
// a method to deal with that shader specifically.
void Renderer::render(Scene* scene,Window* window, Cinematic* cinematic, const AssetManager& assets, float aspect, float dt) {
    RenderContext ctx{};
    ctx.camera = &scene->main_camera;
    ctx.scene = scene;
    ctx.window = window;

    m_drawCalls = 0;

    //time
    time_modulo = time_modulo + dt;
    time = time + dt;
    while(time_modulo>max_time){
        time_modulo -= max_time;
    }

    // Hybrid pass
    m_renderPipeline.clear("Hybrid Render Pass");
    m_renderPipeline.execute("Hybrid Render Pass");

    // render the skybox
    auto skybox = scene->m_skybox;
    auto it = skybox->passTagShaderHandle.find(PassTag::Hybrid);
    if (it == skybox->passTagShaderHandle.end()){
        Log::error("Did not find shader handle for Hybrid Render Pass for object whose meshHandle is "+std::to_string(skybox->meshHandle));
    }
    else {
        auto handle = it->second;
        const Shader& shader = ShaderManager::get(handle);
        shader.bind();
        shader.set("model", glm::translate(glm::mat4(1.0f),scene->main_camera.position));
        shader.set("view", scene->main_camera.view());
        shader.set("projection", scene->main_camera.projection(aspect));
        const Mesh* mesh = assets.get(skybox->meshHandle);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        mesh->draw();
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
    }
    // render the rest of the meshes
    for (unsigned int i = 0; i < scene->objects.size(); i++) {
        auto obj = scene->objects[i];
        auto it = obj.passTagShaderHandle.find(PassTag::Hybrid);
        if (it == obj.passTagShaderHandle.end()){
            Log::error("Did not find shader handle for Hybrid Render Pass for object whose meshHandle is "+std::to_string(obj.meshHandle));
            continue;
        }

        ShaderHandle handle = it->second;
        if (handle == 0) {
            Log::error("shader of obj "+std::to_string(obj.meshHandle)+" is null for hybrid render pass");
            continue;
        };

        const Mesh* mesh = assets.get(obj.meshHandle);

        const Shader& shader = ShaderManager::get(handle);
        shader.bind();
        shader.set("lightsNumber", static_cast<unsigned int>(scene->lights.size()));
        shader.set("model",obj.transform.matrix());
        shader.set("viewPos",scene->main_camera.position);
        shader.set("view",scene->main_camera.view());
        shader.set("projection",scene->main_camera.projection(aspect));
        // shader.set("objectBarycentre",glm::vec3(obj.transform.matrix()*glm::vec4(mesh.pseudo_barycentre(),1.0f)));
        shader.set("objectColor",obj.material.color);
        shader.set("meshId",static_cast<unsigned int>(i+1));
        mesh->uploadUniforms(shader, ctx);
        mesh->draw();
        ++m_drawCalls;
    }

    // draw joints
    ShaderHandle jointShaderHandle = ShaderManager::getShaderHandleWithKey(ShaderKey{"joint_hybrid.vert", "joint_hybrid.frag"});
    if (jointShaderHandle != 0) {
      const Shader& jointShader = ShaderManager::get(jointShaderHandle);
      jointShader.bind();
      jointShader.set("view", scene->main_camera.view());
      jointShader.set("proj", scene->main_camera.projection(aspect));
      jointShader.set("jointColor", glm::vec3(0.0f, 1.0f, 0.0f));

      glDisable(GL_DEPTH_TEST);
      glEnable(GL_PROGRAM_POINT_SIZE);

      GLuint vao;
      glGenVertexArrays(1, &vao);
      glBindVertexArray(vao);

      for (int i = 0; i < scene->objects.size(); i++) {
        auto &obj = scene->objects[i];
        const Mesh *mesh = assets.get(obj.meshHandle);
        const auto *animatedMesh = dynamic_cast<const AnimatedMesh*>(mesh);

        if (animatedMesh) {
          std::vector<glm::mat4> transforms;
          animatedMesh->getBoneTransforms(transforms);
          glm::mat4 objModelMatrix = obj.transform.matrix();
          for (int j = 0; j < transforms.size(); ++j) {
            glm::vec3 jointLocalPos = glm::vec3(transforms[j][3]);
            glm::vec3 jointWorldPos = glm::vec3(objModelMatrix * glm::vec4(jointLocalPos, 1.0f));
            glm::mat4 model = glm::translate(glm::mat4(1.0f), jointWorldPos);
            jointShader.set("model", model);
            jointShader.set("jointId", static_cast<unsigned int>(j));
            glDrawArrays(GL_POINTS, 0, 1);
          }
        }
      }
      glBindVertexArray(0);
      glDeleteVertexArrays(1, &vao);
      glDisable(GL_PROGRAM_POINT_SIZE);
      glEnable(GL_DEPTH_TEST);
    }


    //**************************draw debug for smear******************************
    // ShaderHandle debugHandle = ShaderManager::getShaderHandleWithKey(ShaderKey{"debug_bone.vert", "debug_bone.frag"});
    // if (debugHandle != 0) {
    //   const Shader& debugShader = ShaderManager::get(debugHandle);
    //   debugShader.bind();
    //   debugShader.set("view", scene->main_camera.view());
    //   debugShader.set("projection", scene->main_camera.projection(aspect));

    //   for (unsigned int i = 0; i < scene->objects.size(); i++) {
    //     auto& obj = scene->objects[i];
    //     debugShader.set("model", obj.transform.matrix());
    //     debugShader.set("meshId", static_cast<unsigned int>(i+1));

    //     const Mesh* mesh = assets.get(obj.meshHandle);
    //     const auto* smearMesh = dynamic_cast<const SmearMesh*>(mesh);
    //     if (smearMesh) {
    //       smearMesh->drawDebugBones(debugShader, ctx);
    //     }
    //   }
    // }

    glEnable(GL_DEPTH_TEST);
    // Mesh ID pass
    m_renderPipeline.clear("Mesh ID Render Pass");
    m_renderPipeline.execute("Mesh ID Render Pass");
    for (unsigned int i = 0; i < scene->objects.size(); i++) {
        auto obj = scene->objects[i];
        auto it = obj.passTagShaderHandle.find(PassTag::MeshID);
        if (it == obj.passTagShaderHandle.end()) {continue;}
        ShaderHandle handle = it->second;
        if (handle == 0) {continue;}
    }

    std::vector<unsigned int> hybridFboTexs = m_renderPipeline.getPassFboTexs("Hybrid Render Pass");

    // KAF post-processing (optional)
    unsigned int sceneTexForFinal = hybridFboTexs[0];
    if (useKuwahara) {
        m_renderPipeline.clear("KAF Render Pass");
        m_renderPipeline.execute("KAF Render Pass");
        std::shared_ptr<KAFRenderPass> kaf_pass = std::static_pointer_cast<KAFRenderPass>(m_renderPipeline.getPass("KAF Render Pass"));
        auto shaderHandles = kaf_pass->shaderHandles();
        std::vector<unsigned int> kafFboTexs = kaf_pass->fboTexs();
        const Shader& shader0 = ShaderManager::get(shaderHandles[0]);
        const Shader& shader1 = ShaderManager::get(shaderHandles[1]);
        const Shader& shader2 = ShaderManager::get(shaderHandles[2]);
        int w, h;
        window->getSize(w, h);
        glm::vec2 screenSize(w, h);

        // pass 1 — structure tensor
        shader0.bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hybridFboTexs[0]);
        shader0.set("screenSize", screenSize);
        shader0.set("screenTexture", 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        p_screen_mesh->draw();

        // pass 2 — smooth tensor
        shader1.bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, kafFboTexs[0]);
        shader1.set("screenSize", screenSize);
        shader1.set("tensorTexture", 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT1);
        p_screen_mesh->draw();

        // pass 3 — KAF output
        shader2.bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hybridFboTexs[0]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, kafFboTexs[1]);
        shader2.set("screenSize", screenSize);
        shader2.set("screenTexture", 0);
        shader2.set("tensorTexture", 1);
        glDrawBuffer(GL_COLOR_ATTACHMENT2);
        p_screen_mesh->draw();

        sceneTexForFinal = kafFboTexs[2];
    }

    // Final screen pass
    m_renderPipeline.clear("Final Render Pass");
    m_renderPipeline.execute("Final Render Pass");
    ShaderHandle handle = std::static_pointer_cast<FinalRenderPass>(m_renderPipeline.getPass("Final Render Pass"))->shaderHandle();
    const Shader& shader = ShaderManager::get(handle);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTexForFinal);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, hybridFboTexs[1]);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, hybridFboTexs[2]);
    shader.bind();
    shader.set("sceneTexture", 0);
    shader.set("metadataTexture", 1);
    shader.set("normalTexture", 2);
    int window_w, window_h;
    window->getSize(window_w, window_h);
    shader.set("window_w", window_w);//for final_contours.frag
    shader.set("window_h", window_h);//for final_contours.frag
    //others uniforms
    shader.set("time", time_modulo/max_time); //for final_perlin_background.frag
    //draw mesh
    // cinematic->uploadUniforms(shader, time);
    p_screen_mesh->uploadUniforms(shader, ctx);
    p_screen_mesh->draw();

    ++m_drawCalls;
}

void Renderer::onResize(int width, int height) {
    m_renderPipeline.onResize(width, height);
}

int Renderer::drawCalls() const {
    return m_drawCalls;
}
