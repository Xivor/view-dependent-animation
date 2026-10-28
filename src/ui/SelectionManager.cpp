//
// Created by mathi on 16/05/2026.
//
#include "SelectionManager.h"
#include "renderer/Mesh.h"
#include "core/MegaWindowContext.h"
#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "scene/Scene.h"

//#define DEBUG

void SelectionManager::select(glm::vec2 mousePos, unsigned int window_h) {




    // auto fbo = m_render_pipeline->getPassFbo("Mesh ID Render Pass");

    // if (fbo == 0) {
    //     m_selection = nullptr;
    //     return;
    // }
    // glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);

    // glReadBuffer(GL_COLOR_ATTACHMENT0);

    // int x = static_cast<int>(mousePos.x);
    // int y = static_cast<int>(mousePos.y);

    // GLuint pixelId = 0;

    // glReadPixels(x, window_h - y, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &pixelId);
    // Log::info("pixelId : "+std::to_string(pixelId));

    // glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    int x = static_cast<int>(mousePos.x);
    int y = static_cast<int>(mousePos.y);
    auto fbo = m_render_pipeline->getPassFbo("Hybrid Render Pass");

    if (fbo == 0) {
        m_selection = nullptr;
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);

    glReadBuffer(GL_COLOR_ATTACHMENT1);

    //GLuint pixelId = 0;
    glm::uvec4 pixelMetatData = {0, 0, 0, 0};

    glReadPixels(x, window_h - y, 1, 1, GL_RGBA_INTEGER, GL_UNSIGNED_INT, &pixelMetatData);

    GLuint pixelId = pixelMetatData[0];

    if (pixelId == 0) {
        m_selection = nullptr;
    } else {
        size_t objectIndex = static_cast<size_t>(pixelId - 1);

        auto& objects = m_scene->objects;

        if (objectIndex < objects.size()) {
            m_selection = &objects[objectIndex];
        } else {
            m_selection = nullptr;
        }
    }

    #ifdef DEBUG
    //read value from the hybrid framebuffer
    fbo = m_render_pipeline->getPassFbo("Hybrid Render Pass");

    if (fbo == 0) {
        m_selection = nullptr;
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);

    glReadBuffer(GL_COLOR_ATTACHMENT0);

    //GLuint pixelId = 0;
    glm::vec4 pixelValue = {0.0, 0.0, 0.0, 0.0};

    glReadPixels(x, window_h - y, 1, 1, GL_RGBA, GL_FLOAT, &pixelValue);
    Log::info("hybrid_framebuffer, color_attachment_0 : "+std::to_string(pixelValue[0])+" "+std::to_string(pixelValue[1])+" "+std::to_string(pixelValue[2]));

    glReadBuffer(GL_COLOR_ATTACHMENT1);

    //GLuint pixelId = 0;
    glm::uvec4 upixelValue = {0, 0, 0, 0};

    glReadPixels(x, window_h - y, 1, 1, GL_RGBA_INTEGER, GL_UNSIGNED_INT, &upixelValue);
    Log::info("hybrid_framebuffer, color_attachment_1 : "+std::to_string(upixelValue[0])+" "+std::to_string(upixelValue[1])+" "+std::to_string(upixelValue[2]));


    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
   #endif


}

void SelectionManager::draw() {
    if (m_selection != nullptr) {
        Mesh* mesh = m_assets ? m_assets->get(m_selection->meshHandle) : nullptr;
        m_selection->draw(mesh);
    }
}
