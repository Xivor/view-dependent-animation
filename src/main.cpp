#include <iostream>
#include <fstream>
#include <glad/glad.h>
#include <cxxopts.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <streambuf>
#include <Eigen/Dense>
#include <cstdint>
#include <cstdio>
#include <vector>

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

using namespace std;

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

static bool saveScreenshot(const std::string& path, int x, int y) {
  constexpr int imageSize = 512;
  std::vector<unsigned char> pixels(imageSize * imageSize * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(x, y, imageSize, imageSize, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

  const uint32_t rowSize = (imageSize * 3 + 3) & ~3u;
  const uint32_t dataSize = rowSize * imageSize;
  unsigned char header[54] = {'B', 'M'};
  auto put32 = [&](int offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) header[offset + i] = (value >> (8 * i)) & 0xFF;
  };
  put32(2, 54 + dataSize);
  put32(10, 54);
  put32(14, 40);
  put32(18, imageSize);
  put32(22, imageSize);
  header[26] = 1;
  header[28] = 24;
  put32(34, dataSize);

  std::ofstream file(path, std::ios::binary);
  if (!file) return false;
  file.write(reinterpret_cast<const char*>(header), sizeof(header));
  std::vector<unsigned char> row(rowSize, 0);
  for (int rowIndex = 0; rowIndex < imageSize; ++rowIndex) {
    for (int column = 0; column < imageSize; ++column) {
      const size_t source = (static_cast<size_t>(rowIndex) * imageSize + column) * 3;
      row[column * 3] = pixels[source + 2];
      row[column * 3 + 1] = pixels[source + 1];
      row[column * 3 + 2] = pixels[source];
    }
    file.write(reinterpret_cast<const char*>(row.data()), rowSize);
  }
  return static_cast<bool>(file);
}

RiggedMesh* test_mesh;
glm::mat4 test_modelMatrix;

void setMeshDeformPairs(RiggedMesh* riggedMesh, const glm::mat4& modelMatrix, const std::vector<std::pair<glm::vec3, glm::vec3>>& pairs) {
  riggedMesh->deform_pairs.clear();
  for (const auto& pair : pairs) {
    glm::mat4 inverseModelMatrix = glm::inverse(modelMatrix);
    glm::vec3 localPos1 = glm::vec3(inverseModelMatrix * glm::vec4(pair.first, 1.0f));
    glm::vec3 localPos2 = glm::vec3(inverseModelMatrix * glm::vec4(pair.second, 1.0f));
    riggedMesh->deform_pairs.emplace_back(localPos1, localPos2);
  }
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
  sk[PassTag::Hybrid] = ShaderKey{"static_mesh.vert", "skybox_black.frag"};
  Object sb = Object{sk};
  sb.meshHandle = meshHandle;
  scene.m_skybox = &sb;

  // ShaderKey debugBoneShaderKey = ShaderKey{ "debug_bone.vert", "debug_bone.frag"};
  // ShaderManager::load(debugBoneShaderKey);
  ShaderKey jointShaderKey = ShaderKey{ "joint_hybrid.vert", "joint_hybrid.frag"};
  ShaderManager::load(jointShaderKey);

  // staticMeshData = Geometry::makePlane();
  // meshHandle = assets.add(std::make_unique<StaticMesh>(staticMeshData.vertices, staticMeshData.indices));
  // sk[PassTag::Hybrid] = ShaderKey{"static_mesh.vert","final_chessboard_mesh.frag"};
  // Object obj = Object{sk};
  // obj.meshHandle =meshHandle;
  // obj.material.color = {0.8f, 0.3f, 0.2f};
  // obj.transform.scale = glm::vec3(100.f);
  // scene.objects.push_back(obj);

  // add animated tai lung
  meshHandle = assets.add(std::make_unique<SmearMesh>("assets/meshes/tai lung melee with anim/tai lung melee with anim.fbx"));
  sk = assets.get(meshHandle)->shaderKeysMap();
  // sk[PassTag::Hybrid] = ShaderKey{"hybrid_smear.vert", "hybrid_toon_shading.frag"};

  Object o = Object{sk};
  o.meshHandle       = meshHandle;
  o.material.color   = {0.85f, 0.7f, 0.55f};
  o.transform.scale  = glm::vec3(.02f);
  o.transform.position = glm::vec3(3.f, 2.f, 0.f);
  scene.objects.push_back(o);

  // add static mouse mesh
  std::string base_dir = "assets/RaBit/100/3d_model/";
  meshHandle = assets.add(std::make_unique<RiggedMesh>(
                            base_dir + "m.obj", base_dir + "3d_joints.txt", base_dir + "beta.npy",
      base_dir + "pose.npy")
  );
  sk = assets.get(meshHandle)->shaderKeysMap();

  o = Object{sk};
  o.meshHandle       = meshHandle;
  o.material.color   = {0.85f, 0.7f, 0.55f};
  o.transform.scale  = glm::vec3(5.0f);
  o.transform.position = glm::vec3(0.f, 2.f, 0.f);
  scene.objects.push_back(o);

  test_mesh = dynamic_cast<RiggedMesh*>(assets.get(meshHandle));
  test_mesh->deform_pairs = {
    {glm::vec3(0.f), glm::vec3(0.f, 0.f, 0.05f)},
    {glm::vec3(0.05f, 0.1f, 0.05f), glm::vec3(0.06f, 0.11f, 0.06f)}
  };
  test_modelMatrix = o.transform.matrix();


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

  // TEST with hardcoded model / image
  // works only for RiggedMesh
  RiggedMesh *r;
  if ((r = dynamic_cast<RiggedMesh*>(assets.get(meshHandle))) != nullptr) {
    CameraRecoveryEngine camRecEngine;
    std::vector<glm::mat4> modelJointTransforms = dynamic_cast<RiggedMesh*>(assets.get(meshHandle))->m_joint_transforms; // HACK changing the type of mesh breaks this

    // 2d points
    std::map<int, glm::vec2> imageById;
    std::string path = "assets/RaBit/100/2d_drawing/2d_joints.txt";
    std::ifstream file(path);
    if (!file.is_open()) {
      Log::error("failed to open joints file " + path);
    }
    std::string line;
    while(std::getline(file, line)) {
      std::stringstream ss(line);
      int index;
      float x, y;
      int status;
      if (!(ss >> index >> x >> y >> status)) {
        continue;
      }
      if (status == -1) continue;
      imageById[index] = glm::vec2(x, y);
    }

    std::vector<glm::vec3> modelJoints;
    std::vector<glm::vec2> imageJoints;
    for (const auto& [id, imagePoint] : imageById) {
      if (id < 0 ||
          id >= static_cast<int>(modelJointTransforms.size())) {
        continue;
      }
      glm::vec3 modelPoint =
        glm::vec3(modelJointTransforms[id][3]);

      modelJoints.push_back(modelPoint);
      imageJoints.push_back(imagePoint);

      std::cout << "Matching 3D joint " << id
                << " with 2D joint " << id << std::endl;
    }

    vector<pair<int, int>> match = {
        {1, 1},
        {2, 2},
        {3, 3},
        // {4, 17},
        {5, 18},
        // {6, 19},
        {7, 20},
        {8, 21},
        {9, 9},
        {10, 10},
        // {11, 13},
        // {12, 14},
        // {13, 11},
        // {14, 12},
        {22, 15},
        {23, 16},
        {15, 22},
        {16, 23},
        // {17, 4},
        {18, 5},
        // {19, 6},
        {20, 7},
        {21, 8},
  }; // which IDs mesh / image to match

    vector<glm::vec3> selectedModelJoints;
    vector<glm::vec2> selectedImageJoints;
    for (const auto& [meshId, imageId] : match) {
      if (meshId < 0 || meshId >= static_cast<int>(modelJointTransforms.size())) {
        std::cerr << "Invalid 3D joint ID: "
                  << meshId << std::endl;
        continue;
      }
      auto it = imageById.find(imageId);
      if (it == imageById.end()) {
        std::cerr << "Missing 2D joint ID: "
                  << imageId << std::endl;
        continue;
      }
      glm::vec3 modelPoint = glm::vec3(modelJointTransforms[meshId][3]);
      glm::vec2 imagePoint = it->second;

      selectedModelJoints.push_back(modelPoint);
      selectedImageJoints.push_back(imagePoint);

      std::cout
        << "3D joint " << meshId
        << " (" << glm::to_string(modelPoint) << ")"
        << " -> 2D joint " << imageId
        << " (" << glm::to_string(imagePoint) << ")"
        << std::endl;
    }

    if (selectedModelJoints.size() < 6)
    {
      throw std::runtime_error(
        "Not enough valid correspondences for DLT"
      );
    }

    std::cout << "Total correspondences: "
              << selectedModelJoints.size() << std::endl;

    Eigen::Matrix<double, 3, 4> camMatrix = camRecEngine.RecoverCamera(selectedImageJoints, selectedModelJoints);
    Eigen::JacobiSVD<Eigen::Matrix<double, 3, 4>> svd(camMatrix, Eigen::ComputeFullV);
    Eigen::Vector4d C = svd.matrixV().col(3);
    glm::vec3 cameraPositionLocal = glm::vec3(C(0), C(1), C(2)) / static_cast<float>(C(3));

    glm::vec4 rootLocalPos = modelJointTransforms[0][3];
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), o.transform.position)
      * glm::mat4_cast(o.transform.rotation)
      * glm::scale(glm::mat4(1.0f), o.transform.scale);
    glm::vec3 T = glm::vec3(modelMatrix * rootLocalPos);
    glm::vec3 cameraPositionGlobal = glm::vec3(modelMatrix * glm::vec4(cameraPositionLocal, 1.0f));
    scene.main_camera.position = cameraPositionGlobal;
    // scene.main_camera.target = glm::normalize(T - cameraPositionGlobal);
    scene.main_camera.target = glm::normalize(T);

    // TODO use the found intrinsics in the camera parameters as well

    std::cout << "found camera position: " << glm::to_string(cameraPositionGlobal) << std::endl;
    std::cout << "found camera direction: " << glm::to_string(scene.main_camera.target) << std::endl;
  }

  else {
    //camera
    scene.main_camera.position = {0.f, 1.f, 5.f};
    scene.main_camera.target   = {0.f, 0.f, 0.f};
  }

  Light light;

  light.position  = {0.f, 50.f, -150.f};
  light.color = {1.0,1.0,0};
  light.intensity = 1.f;
  scene.lights.push_back(light);

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
  bool prevF5 = false;
  bool prevF6 = false;
  bool prevP = false;
  bool prevK = false;
  bool screenshotRequested = false;
  int screenshotCount = 0;
  CameraImageProjection screenshotProjection;

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

    // F5 - test vertex change
    bool f5 = glfwGetKey(window.handle(), GLFW_KEY_F5) == GLFW_PRESS;
    if (f5 && !prevF5) {
      test_mesh->test_vertex_change();
    }
    prevF5 = f5;

    // F6 - toggle wireframe (applied by the Renderer, only to the scene meshes)
    bool f6 = glfwGetKey(window.handle(), GLFW_KEY_F6) == GLFW_PRESS;
    if (f6 && !prevF6) renderer.wireframe = !renderer.wireframe;
    prevF6 = f6;

    // P - take screenshot (512x512) of the current frame
    bool currP = glfwGetKey(window.handle(), GLFW_KEY_P) == GLFW_PRESS;
    if (currP && !prevP) screenshotRequested = true;
    prevP = currP;

    // K - toggle keypoints (applied by the Renderer, only to the scene meshes)
    bool currK = glfwGetKey(window.handle(), GLFW_KEY_K) == GLFW_PRESS;
    if (currK && !prevK) renderer.showKeypoints = !renderer.showKeypoints;
    prevK = currK;

    // ZQSD camera movement
    camCtrl.update(scene.main_camera, window.handle(), dt);

    int w, h;
    window.getSize(w, h);

    float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
    renderer.render(&scene, &window, &cinematic, assets, aspect, dt);

    if (screenshotRequested) {
      constexpr int imageSize = 512;
      if (w >= imageSize && h >= imageSize) {
        const int cropX = (w - imageSize) / 2;
        const int cropY = (h - imageSize) / 2;
        screenshotProjection.viewMatrix = scene.main_camera.view();
        screenshotProjection.projectionMatrix = scene.main_camera.projection(aspect);
        screenshotProjection.framebufferSize = {w, h};
        screenshotProjection.cropOrigin = {cropX, h - cropY - imageSize};
        screenshotProjection.imageSize = {imageSize, imageSize};
        screenshotProjection.target = scene.main_camera.target;
        screenshotProjection.depthValues.resize(imageSize * imageSize);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, renderer.renderPipeline()->getPassFbo("Hybrid Render Pass"));
        glReadPixels(cropX, cropY, imageSize, imageSize, GL_DEPTH_COMPONENT, GL_FLOAT,
               screenshotProjection.depthValues.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        char filename[64];
        std::snprintf(filename, sizeof(filename), "screenshot_%03d.bmp", screenshotCount++);
        if (saveScreenshot(filename, cropX, cropY)) {
          Log::info(std::string("Saved 512x512 screenshot: ") + filename);
        } else {
          Log::error(std::string("Could not write screenshot: ") + filename);
        }
      } else {
        Log::warn("Screenshot needs a framebuffer of at least 512x512 pixels");
      }
      screenshotRequested = false;
      glm::vec3 originalPos = screenshotProjection.backToSpace(glm::vec2(100.f, 100.f));
      glm::vec3 targetPos = screenshotProjection.closestProjection(glm::vec2(100.f, 110.f), originalPos);
      std::vector<std::pair<glm::vec3, glm::vec3>> deformPairs = {
        {originalPos, targetPos}
      };
      setMeshDeformPairs(test_mesh, test_modelMatrix, deformPairs);
    }

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
