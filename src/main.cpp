#include <iostream>
#include <glad/glad.h>
#include <cxxopts.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <streambuf>

#include "core/Log.h"
#include "core/Window.h"
#include "core/AssetManager.h"
#include "core/MegaWindowContext.h"
#include "renderer/AnimatedMesh.h"
#include "renderer/SmearMesh.h"
#include "renderer/RiggedMesh.h"
#include "scene/Scene.h"
#include "scene/CameraController.h"
#include "scene/Cinematic.h"
#include "renderer/Geometry.h"
#include "renderer/Renderer.h"
#include "renderer/ShaderManager.h"
#include "ui/DebugConsole.h"
#include "ui/RenderStats.h"
#include "ui/SelectionManager.h"
#include "view-dependent-deformations/CameraRecoveryEngine.h"

static void setupGLDebugCallback() {
  glEnable(GL_DEBUG_OUTPUT);
     glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
     glDebugMessageCallback([](GLenum, GLenum type, GLuint, GLenum severity,
     GLsizei, const GLchar* message, const void*) {
       if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
     if (type == GL_DEBUG_TYPE_ERROR)
       Log::error(std::string("GL: ") + message);
     else
       Log::warn(std::string("GL: ") + message);
     }, nullptr);
}


int main(int argc, char *argv[]) {
  // TODO direct some of the output to a file
  // const char* outFilename = "output.txt";
  // std::string outFile(outFilename);
  // std::streambuf* coutbuf = std::cout.rdbuf();
  // termbuf = std::cout

  cxxopts::Options options("Stylized Animation", "A stylized animation engine");
  options.add_options()
    ("s,save-video", "save video with ffmpeg")
    ("H,headless", "prevents window from popping up. Useful for just generating a video")
    ("f,frames", "specify a number of frames to generate", cxxopts::value<int>()->default_value("-1"))
    ("t,target-fps", "target fps of the animation", cxxopts::value<int>()->default_value("-1"))
    ("h,help", "shows usage")
    ("d,debug", "activate debug features");
  auto result = options.parse(argc, argv);
  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  bool saveVideo = result.count("save-video");
  bool headless = result.count("headless");
  int frames = result["frames"].as<int>();
  int targetFps = result["target-fps"].as<int>();
  bool debug = result.count("debug");

  if (headless) {
    glfwInit();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  }
  Window window(1280, 720, "Stylized Animation Engine", headless);
  setupGLDebugCallback();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window.handle(), true);
  ImGui_ImplOpenGL3_Init("#version 460");
  // creating the assets manager (to not have two meshes duplicated in memory)
  AssetManager assets;

  // creating the scene and initializing memory space once.
  Scene scene;
  MeshHandle meshHandle = 0;
  std::map<PassTag, ShaderKey> sk;
  StaticMeshData staticMeshData;

  // creating the skybox
  staticMeshData = Geometry::makeCube();
  meshHandle = assets.add(std::make_unique<StaticMesh>(staticMeshData.vertices, staticMeshData.indices));
  sk[PassTag::Hybrid] = ShaderKey{"static_mesh.vert", "hybrid_skybox.frag"};
  Object sb = Object{sk};
  sb.meshHandle = meshHandle;
  scene.m_skybox = &sb;

  ShaderKey debugBoneShaderKey = ShaderKey{ "debug_bone.vert", "debug_bone.frag"};
  ShaderManager::load(debugBoneShaderKey);
  ShaderKey jointShaderKey = ShaderKey{ "joint_hybrid.vert", "joint_hybrid.frag"};
  ShaderManager::load(jointShaderKey);

  staticMeshData = Geometry::makePlane();
  meshHandle = assets.add(std::make_unique<StaticMesh>(staticMeshData.vertices, staticMeshData.indices));
  sk[PassTag::Hybrid] = ShaderKey{"static_mesh.vert","final_chessboard_mesh.frag"};
  Object obj = Object{sk};
  obj.meshHandle =meshHandle;
  obj.material.color = {0.8f, 0.3f, 0.2f};
  obj.transform.scale = glm::vec3(100.f);
  scene.objects.push_back(obj);

  // meshHandle = assets.add(std::make_unique<SmearMesh>("assets/meshes/tai lung melee with anim/tai lung melee with anim.fbx"));
  std::string base_dir = "assets/RaBit/100/3d_model/";
  meshHandle = assets.add(std::make_unique<RiggedMesh>(
                            base_dir + "m.obj", base_dir + "3d_joints.txt", base_dir + "beta.npy",
      base_dir + "pose.npy")
  );
  sk = assets.get(meshHandle)->shaderKeysMap();
  // sk[PassTag::Hybrid] = ShaderKey{"hybrid_smear.vert", "hybrid_toon_shading.frag"};

  Object o = Object{sk};
  o.meshHandle       = meshHandle;
  o.material.color   = {0.85f, 0.7f, 0.55f};
  o.transform.scale  = glm::vec3(5.0f);
  o.transform.position = glm::vec3(0.f, 2.f, 0.f);
  scene.objects.push_back(o);
  // {
  //   constexpr int   GRID    = 3;//5
  //   constexpr float SPACING = 2.0f;
  //   constexpr float OFFSET  = (GRID - 1) * SPACING * 0.5f;
  //   for (int row = 0; row < GRID; row++) {
  //     for (int col = 0; col < GRID; col++) {
  //       Object o = Object{sk};
  //       o.meshHandle       = meshHandle;
  //       o.material.color   = {0.85f, 0.7f, 0.55f};
  //       o.transform.scale  = glm::vec3(0.01f);
  //       o.transform.position = glm::vec3(col * SPACING - OFFSET, 0.f, row * SPACING - OFFSET);
  //       scene.objects.push_back(o);
  //     }
  //   }
  // }

  CameraRecoveryEngine camRecEngine = new CameraRecoveryEngine();

  Light light;

  light.position  = {0.f, 50.f, -150.f};
  light.color = {1.0,1.0,0};
  light.intensity = 1.f;
  scene.lights.push_back(light);

  //camera
  scene.main_camera.position = {0.f, 1.f, 5.f};
  scene.main_camera.target   = {0.f, 0.f, 0.f};


  //cinematic
  Cinematic cinematic;


  // instancing the necessary elements to render the scene
  Renderer         renderer;
  CameraController camCtrl;
  DebugConsole     console;
  RenderStats      stats;
  SelectionManager selectionManager{&scene, renderer.renderPipeline(), &assets};

  // Compiling shaders
  // specially for the skybox
  for (auto& pair : scene.m_skybox->passTagShaderSpecifications) {
    auto& render_pass = pair.first;
    auto& shader_key = pair.second;
    scene.m_skybox->passTagShaderHandle[render_pass] = ShaderManager::load(shader_key);
  }

  // the rest of the meshes
  for (auto& object : scene.objects) {
    for (auto& pair : object.passTagShaderSpecifications) {
      auto& render_pass = pair.first;
      auto& shader_key = pair.second;
      object.passTagShaderHandle[render_pass] = ShaderManager::load(shader_key);
    }
  }

  // Setup the renderer
  int w,h;
  window.getSize(w, h);
  auto aspect = h > 0 ? static_cast<float>(w)/static_cast<float>(h) : 0;
  renderer.setup(&scene,&window,assets,aspect);

  // Set up all necessary callbacks

  MegaWindowContext windowContext;
  windowContext.scene = &scene;
  windowContext.renderer = &renderer;
  windowContext.selectionManager = &selectionManager;
  windowContext.window = &window;
  windowContext.cameraController = &camCtrl;

  // Here, you tell glfw to give you windowContext instead of window when you will
  // call glfwGetWindowUserPointer in the future
  glfwSetWindowUserPointer(window.handle(), &windowContext);

  glfwSetFramebufferSizeCallback(window.handle(), [](GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    MegaWindowContext* windowContext = static_cast<MegaWindowContext*>(glfwGetWindowUserPointer(window));
    if (!windowContext) {
      return;
    }
    if (auto* renderer = windowContext->renderer) {
      renderer->onResize(width,height);
    }
  });

  glfwSetMouseButtonCallback(window.handle(), [](GLFWwindow* window, int button, int action, int mods) {

    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    auto* ctx = static_cast<MegaWindowContext*>(glfwGetWindowUserPointer(window));
    if (!ctx) {
      return;
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
      if (ctx->selectionManager) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        int w,h;
        ctx->window->getSize(w,h);
        ctx->selectionManager->select(glm::vec2(xpos, ypos),h);
      }
    }
  });

  Log::info("Engine ready");
  Log::info("F1 -> toggle camera control | F2 -> reload all shaders | F4 -> show/hide UI");

  double prevTime = glfwGetTime();
  int currentFrame = 0;
  float  fps      = 0.f;
  bool paused = false;
  bool showUI = true;
  // Edge-trigger state for toggle keys
  bool prevF1 = false;
  bool prevF2 = false;
  bool prevF3 = false;
  bool prevF4 = false;

  // Record animation
  int vid_w = 1280;
  int vid_h = 720;
  FILE* ffmpeg = nullptr;
  int* vidBuffer = nullptr;
  if (saveVideo == true) {
    const char* cmd = "ffmpeg -r 60 -f rawvideo -pix_fmt rgba -s 1280x720 -i - "
      "-threads 0 -preset fast -y -pix_fmt yuv420p -crf 21 -vf vflip output.mp4";
    ffmpeg = popen(cmd, "w");
    vidBuffer = new int[vid_w * vid_h];
  }


  //restart
  printf("program ready\n");
  for (auto& obj : scene.objects) {
    auto animated_mesh = dynamic_cast<AnimatedMesh*>(assets.get(obj.meshHandle));
    if (animated_mesh) {
      animated_mesh->restartTimer();
    }
  }


  while (!window.shouldClose() && (frames <= 0 || currentFrame < frames)) {
    window.pollEvents();

    double now = glfwGetTime();
    float  dt  = static_cast<float>(now - prevTime);
    prevTime   = now;
    fps        = dt > 0.f ? 1.f / dt : 0.f;

    if (targetFps > 0) {
      dt = 1.0f / targetFps;
    }

    // F1 - toggle camera control
    bool f1 = glfwGetKey(window.handle(), GLFW_KEY_F1) == GLFW_PRESS;
    if (f1 && !prevF1) {
      camCtrl.setEnabled(window.handle(), !camCtrl.enabled);
      Log::info(camCtrl.enabled ? "Camera control ON" : "Camera control OFF");
    }
    prevF1 = f1;

    // F2 - hot-reload all shaders from disk
    bool f2 = glfwGetKey(window.handle(), GLFW_KEY_F2) == GLFW_PRESS;
    if (f2 && !prevF2){
      ShaderManager::reloadAll();
    }
    prevF2 = f2;
    // F3 - Pause animation
    bool f3 = glfwGetKey(window.handle(), GLFW_KEY_F3) == GLFW_PRESS;
    if (f3 && !prevF3) {
      for (auto& obj : scene.objects) {
        auto animated_mesh = dynamic_cast<AnimatedMesh*>(assets.get(obj.meshHandle));
        if (animated_mesh) {
          auto timer = animated_mesh->getTimer();
          paused = !paused;
          if (paused) {
            timer->pause();
          }
          else {
            timer->start();
          }
        }
      }
    }
    prevF3 = f3;

    // F4 - show/hide UI
    bool f4 = glfwGetKey(window.handle(), GLFW_KEY_F4) == GLFW_PRESS;
    if (f4 && !prevF4) showUI = !showUI;
    prevF4 = f4;

    // ZQSD camera movement
    camCtrl.update(scene.main_camera, window.handle(), dt);

    int w, h;
    window.getSize(w, h);

    float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
    renderer.render(&scene, &window, &cinematic, assets, aspect, dt);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    if (showUI) {
      selectionManager.draw();
      console.draw();
      stats.draw(window, renderer.drawCalls(), assets.meshCount(), fps, assets.get(meshHandle)->getCurrentFrame());
      ShaderManager::drawUI();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (saveVideo == true) {
      glReadPixels(0, 0, vid_w, vid_h, GL_RGBA, GL_UNSIGNED_BYTE, vidBuffer);
      fwrite(vidBuffer, sizeof(int) * vid_w * vid_h, 1, ffmpeg);
    }
    window.swapBuffers();
    currentFrame++;
  }

  if (saveVideo){
    std::cout << "video saved!"  << std::endl;
  }

  ShaderManager::shutdown();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  if (saveVideo == true) {
    pclose(ffmpeg);
  }

  return 0;
}
