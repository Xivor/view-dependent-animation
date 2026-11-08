//
// Created by mathi on 16/05/2026.
//

#ifndef STYLIZE_ANIMATION_SELECTIONMANAGER_H
#define STYLIZE_ANIMATION_SELECTIONMANAGER_H

#include <type_traits>
#include <unordered_set>
#include <glm/glm.hpp>

#include "renderer/RenderPipeline.h"
#include "core/AssetManager.h"
class Mesh;
class Camera;
class Scene;
class Object;

/**
 * @brief works with an IPicker. Returns the selection of meshes the user selected.
 */
class SelectionManager {
    Object* m_selection = nullptr;
    Scene* m_scene = nullptr;
    RenderPipeline* m_render_pipeline = nullptr;
    AssetManager* m_assets = nullptr;
  int m_selectedJointId = -1;
public:
    SelectionManager(Scene* scene, RenderPipeline* render_pipeline, AssetManager* assets)
        : m_scene(scene), m_render_pipeline(render_pipeline), m_assets(assets) {}

    /**
     * @brief read inside the fbo used for the first rendering pass, and return the object drawn at this pos
     */
    void select(glm::vec2 mousePos, unsigned int window_h);
    Object* getSelectedObject() const { return m_selection; }
    int getSelectedJointIndex() const { return m_selectedJointId; }
    void draw();
    const auto& getSelection() const {return m_selection;}
};



#endif //STYLIZE_ANIMATION_SELECTIONMANAGER_H
