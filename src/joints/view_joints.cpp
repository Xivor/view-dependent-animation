// Minimal OpenGL (GLFW, legacy fixed pipeline) viewer for a RaBit mesh + skeleton.
//
// Inputs:
//   joints.txt : exported by rabit_joints.py, lines "index parent x y z name"
//   m.obj      : (optional) mesh saved from the SAME beta/pose/trans
//
// Build:
//   Linux:   g++ view_joints.cpp -o view_joints -lglfw -lGL
//   macOS:   g++ view_joints.cpp -o view_joints -lglfw -framework OpenGL
//   Windows: link against glfw3 and opengl32
//
// Run:  ./view_joints joints.txt [m.obj] [--shot out.bmp]
// Controls: left-drag rotate | scroll zoom | M mesh | W wireframe | S skeleton
//           P screenshot (screenshot_000.bmp, 001, ...) | Esc quit
// --shot out.bmp : render one frame, save it to out.bmp and exit.

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct Joint { int parent; float x, y, z; std::string name; };
struct Vec3 { float x, y, z; };

static std::vector<Joint> joints;
static std::vector<Vec3> verts;
static std::vector<std::vector<int>> faces;   // polygons (triangles, quads, ...)
static std::vector<Vec3> faceNormals;

static float yaw = 0.f, pitch = 0.f, dist = 3.f;
static double lastX = 0, lastY = 0;
static bool dragging = false;
static bool showMesh = true, wireframe = false, showSkeleton = true;
static bool shotRequested = false, quitAfterShot = false;
static std::string shotFile;
static int shotCount = 0;

static bool loadJoints(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        int idx;
        Joint j;
        if (ss >> idx >> j.parent >> j.x >> j.y >> j.z) {
            ss >> j.name;
            joints.push_back(j);
        }
    }
    return !joints.empty();
}

// Reads "v" and "f" lines; supports f a, a/b, a/b/c, a//c and negative indices.
static bool loadObj(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            Vec3 v;
            ss >> v.x >> v.y >> v.z;
            verts.push_back(v);
        } else if (tag == "f") {
            std::vector<int> face;
            std::string tok;
            while (ss >> tok) {
                int i = std::stoi(tok.substr(0, tok.find('/')));
                face.push_back(i > 0 ? i - 1 : (int)verts.size() + i);
            }
            if (face.size() >= 3) faces.push_back(face);
        }
    }
    return !verts.empty() && !faces.empty();
}

// Center and scale mesh (or joints if no mesh) to a unit sphere; apply the SAME
// transform to both so they stay aligned.
static void normalize() {
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    auto acc = [&](float x, float y, float z) {
        float p[3] = {x, y, z};
        for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], p[k]); mx[k] = std::max(mx[k], p[k]); }
    };
    if (!verts.empty()) for (auto& v : verts) acc(v.x, v.y, v.z);
    else for (auto& j : joints) acc(j.x, j.y, j.z);

    float c[3], r = 1e-6f;
    for (int k = 0; k < 3; ++k) { c[k] = 0.5f * (mn[k] + mx[k]); r = std::max(r, 0.5f * (mx[k] - mn[k])); }
    for (auto& j : joints) { j.x = (j.x - c[0]) / r; j.y = (j.y - c[1]) / r; j.z = (j.z - c[2]) / r; }
    for (auto& v : verts)  { v.x = (v.x - c[0]) / r; v.y = (v.y - c[1]) / r; v.z = (v.z - c[2]) / r; }
}

// Flat per-face normals (Newell's method, works for quads and n-gons).
static void computeNormals() {
    faceNormals.resize(faces.size());
    for (size_t f = 0; f < faces.size(); ++f) {
        Vec3 n = {0, 0, 0};
        const auto& fc = faces[f];
        for (size_t i = 0; i < fc.size(); ++i) {
            const Vec3& a = verts[fc[i]];
            const Vec3& b = verts[fc[(i + 1) % fc.size()]];
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
        }
        float l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (l > 0) { n.x /= l; n.y /= l; n.z /= l; }
        faceNormals[f] = n;
    }
}

static void onMouseButton(GLFWwindow* w, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        dragging = (action == GLFW_PRESS);
        glfwGetCursorPos(w, &lastX, &lastY);
    }
}
static void onCursor(GLFWwindow*, double x, double y) {
    if (dragging) {
        yaw += (float)(x - lastX) * 0.4f;
        pitch = std::max(-90.f, std::min(90.f, pitch + (float)(y - lastY) * 0.4f));
    }
    lastX = x; lastY = y;
}
static void onScroll(GLFWwindow*, double, double dy) {
    dist = std::max(1.2f, std::min(20.f, dist - (float)dy * 0.2f));
}
static void onKey(GLFWwindow*, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_M) showMesh = !showMesh;
    if (key == GLFW_KEY_W) wireframe = !wireframe;
    if (key == GLFW_KEY_S) showSkeleton = !showSkeleton;
    if (key == GLFW_KEY_P && !shotRequested) {
        char name[64];
        std::snprintf(name, sizeof(name), "screenshot_%03d.bmp", shotCount++);
        shotFile = name;
        shotRequested = true;
    }
}

// Reads the back buffer and writes a 24-bit BMP (no extra libraries needed).
static bool saveScreenshot(const std::string& path, int w, int h) {
    std::vector<unsigned char> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());   // bottom-up rows, as BMP wants

    uint32_t rowSize = ((uint32_t)w * 3 + 3) & ~3u;
    uint32_t dataSize = rowSize * (uint32_t)h, fileSize = 54 + dataSize;
    unsigned char hdr[54] = {'B', 'M'};
    auto put32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; ++i) hdr[o + i] = (v >> (8 * i)) & 0xFF; };
    put32(2, fileSize); put32(10, 54); put32(14, 40);
    put32(18, (uint32_t)w); put32(22, (uint32_t)h);
    hdr[26] = 1; hdr[28] = 24; put32(34, dataSize);

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)hdr, 54);
    std::vector<unsigned char> row(rowSize, 0);
    for (int y = 0; y < h; ++y) {
        const unsigned char* src = &px[(size_t)y * w * 3];
        for (int x = 0; x < w; ++x) {            // RGB -> BGR
            row[x * 3 + 0] = src[x * 3 + 2];
            row[x * 3 + 1] = src[x * 3 + 1];
            row[x * 3 + 2] = src[x * 3 + 0];
        }
        f.write((const char*)row.data(), rowSize);
    }
    return true;
}

static void drawMesh() {
    if (wireframe) {
        glDisable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glColor3f(0.55f, 0.65f, 0.8f);
    } else {
        glEnable(GL_LIGHTING);
        glEnable(GL_COLOR_MATERIAL);
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColor3f(0.75f, 0.78f, 0.85f);
    }
    for (size_t f = 0; f < faces.size(); ++f) {
        glBegin(GL_POLYGON);   // faces from RaBit are convex quads
        glNormal3f(faceNormals[f].x, faceNormals[f].y, faceNormals[f].z);
        for (int i : faces[f]) glVertex3f(verts[i].x, verts[i].y, verts[i].z);
        glEnd();
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_LIGHTING);
}

static void drawSkeleton() {
    glDisable(GL_DEPTH_TEST);   // draw on top so it is visible through the mesh
    glLineWidth(3.f);
    glColor3f(1.f, 1.f, 1.f);
    glBegin(GL_LINES);
    for (size_t i = 0; i < joints.size(); ++i) {
        int p = joints[i].parent;
        if (p < 0 || p >= (int)joints.size()) continue;
        glVertex3f(joints[p].x, joints[p].y, joints[p].z);
        glVertex3f(joints[i].x, joints[i].y, joints[i].z);
    }
    glEnd();
    glPointSize(10.f);
    glBegin(GL_POINTS);
    for (const auto& j : joints) {
        if (j.parent < 0) glColor3f(1.f, 0.9f, 0.1f); else glColor3f(1.f, 0.2f, 0.2f);
        glVertex3f(j.x, j.y, j.z);
    }
    glEnd();
    glEnable(GL_DEPTH_TEST);
}

int main(int argc, char** argv) {
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc) {
            shotFile = argv[++i];
            shotRequested = quitAfterShot = true;
        } else {
            pos.push_back(a);
        }
    }
    const std::string path = "assets/RaBit/" + std::string(argv[1]) + "/3d_model";
    const std::string jointsPath = path + "/3d_joints.txt";
    const std::string meshPath = path + "/m.obj";
    if (!loadJoints(jointsPath)) {
        std::cerr << "Usage: " << argv[1] << "\n";
        return 1;
    }
    if (!loadObj(meshPath)) {
        std::cerr << "Could not load mesh: " << "\n";
        return 1;
    }
    normalize();
    if (!verts.empty()) computeNormals();

    if (!glfwInit()) return 1;
    GLFWwindow* win = glfwCreateWindow(900, 900, "RaBit mesh + joints", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSetMouseButtonCallback(win, onMouseButton);
    glfwSetCursorPosCallback(win, onCursor);
    glfwSetScrollCallback(win, onScroll);
    glfwSetKeyCallback(win, onKey);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POINT_SMOOTH);
    glEnable(GL_NORMALIZE);

    while (!glfwWindowShouldClose(win)) {
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspect = h > 0 ? (float)w / h : 1.f;
        float nearP = 0.1f, top = nearP * std::tan(0.5f * 45.f * 3.14159265f / 180.f);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-top * aspect, top * aspect, -top, top, nearP, 100.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        // Headlight: set in eye space, before the model transforms
        GLfloat lightPos[4] = {0.f, 0.f, 1.f, 0.f};
        glEnable(GL_LIGHT0);
        glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

        glTranslatef(0.f, 0.f, -dist);
        glRotatef(pitch, 1.f, 0.f, 0.f);
        glRotatef(yaw, 0.f, 1.f, 0.f);

        if (showMesh && !verts.empty()) drawMesh();
        if (showSkeleton) drawSkeleton();

        if (shotRequested) {   // capture before swapping, while the back buffer holds this frame
            if (saveScreenshot(shotFile, w, h)) std::cout << "Saved " << shotFile << "\n";
            else std::cerr << "Could not write " << shotFile << "\n";
            shotRequested = false;
            if (quitAfterShot) break;
        }

        glfwSwapBuffers(win);
        glfwPollEvents();
    }
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
