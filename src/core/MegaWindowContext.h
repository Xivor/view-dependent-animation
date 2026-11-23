#pragma once


#include "ui/CameraControls.h"
class CameraController;
struct Scene;
class SelectionManager;
class Renderer;
class Window;

struct MegaWindowContext {
    Renderer* renderer = nullptr;
    SelectionManager* selectionManager = nullptr;
    Window* window = nullptr;
    Scene* scene = nullptr;
    CameraController* cameraController = nullptr;
};
