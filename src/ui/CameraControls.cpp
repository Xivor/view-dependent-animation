#include "CameraControls.h"
#include <imgui.h>
#include "core/MegaWindowContext.h"
#include "core/Window.h"
#include "scene/CameraController.h"
#include "GLFW/glfw3.h"
#include "scene/Scene.h"

void CameraControls::draw(const Window &window, int drawCalls) {
    MegaWindowContext* window_context = static_cast<MegaWindowContext*>(glfwGetWindowUserPointer(window.handle()));
    ImGui::Begin("Camera Controls");

    Camera& cam = window_context->scene->main_camera;
    ImGui::SliderFloat("Camera fov", &cam.fov, 0.1f, 90.0f);

    ImGui::End();
}
