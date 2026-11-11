// Minimal OpenGL (GLFW, legacy fixed pipeline) viewer for a textured RaBit mesh + eyes + skeleton.
//
// Reads, for the given id:  assets/RaBit/<id>/3d_model/
//   3d_joints.txt : exported by rabit_joints.py, lines "index parent x y z name"
//   m.obj (+ m.mtl, m.bmp) : body mesh and its texture
//   e.obj (+ e.mtl, e.bmp) : eyes mesh and its texture (optional)
// The texture file is taken from "map_Kd" in the .mtl (referenced by "mtllib" in the .obj);
// if that fails, <objname>.bmp / .BMP next to the .obj is used. Textures must be .bmp
// (8, 24 or 32 bit, uncompressed). All meshes must come from the SAME beta/pose/trans.
//
// Build:
//   Linux:   g++ view_joints.cpp -o view_joints -lglfw -lGL
//   macOS:   g++ view_joints.cpp -o view_joints -lglfw -framework OpenGL
//   Windows: link against glfw3 and opengl32
//
// Run:  ./view_joints <id> [--shot out.bmp]
// Controls: left-drag rotate | scroll zoom
//           M mesh | T texture on/off | E eyes | W wireframe | S skeleton
//           P screenshot (screenshot_000.bmp, 001, ...) | Esc quit
// --shot out.bmp : render one frame, save it to out.bmp and exit.

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

struct Joint { int parent; float x, y, z; std::string name; };
struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

struct Mesh {
    std::string path;                       // .obj path
    std::string mtllib;                     // from "mtllib" in the .obj
    std::vector<Vec3> v;
    std::vector<Vec2> uv;
    std::vector<std::vector<int>> f, fuv;   // vertex / uv indices per face (polygons)
    std::vector<Vec3> fn;                   // flat face normals
    GLuint tex = 0;
    bool hasTex = false;
};

static std::vector<Joint> joints;
static Mesh body, eyes;
static bool hasEyes = false;

static float yaw = 0.f, pitch = 0.f, dist = 3.f;
static double lastX = 0, lastY = 0;
static bool dragging = false;
static bool showMesh = true, showEyes = true, useTexture = true, wireframe = false, showSkeleton = true;
static bool shotRequested = false, quitAfterShot = false;
static std::string shotFile;
static int shotCount = 0;

// ---------------------------------------------------------------- file helpers
static std::string dirOf(const std::string& p) {
    size_t k = p.find_last_of("/\\");
    return k == std::string::npos ? "" : p.substr(0, k + 1);
}
static std::string baseOf(const std::string& p) {
    size_t k = p.find_last_of("/\\");
    return k == std::string::npos ? p : p.substr(k + 1);
}
static std::string stemOf(const std::string& p) {
    std::string b = baseOf(p);
    size_t k = b.find_last_of('.');
    return k == std::string::npos ? b : b.substr(0, k);
}
static bool fileExists(const std::string& p) { std::ifstream f(p, std::ios::binary); return (bool)f; }
static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

// ---------------------------------------------------------------- loading
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

static int objIndex(const std::string& s, size_t count) {
    int i = std::stoi(s);
    return i > 0 ? i - 1 : (int)count + i;
}

// Reads v, vt, f (a, a/b, a/b/c, a//c, negative indices) and mtllib.
static bool loadObj(const std::string& path, Mesh& m) {
    std::ifstream f(path);
    if (!f) return false;
    m.path = path;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            Vec3 v;
            ss >> v.x >> v.y >> v.z;
            m.v.push_back(v);
        } else if (tag == "vt") {
            Vec2 t;
            ss >> t.x >> t.y;
            m.uv.push_back(t);
        } else if (tag == "mtllib") {
            ss >> m.mtllib;
        } else if (tag == "f") {
            std::vector<int> face, fuv;
            std::string tok;
            while (ss >> tok) {
                size_t a = tok.find('/');
                face.push_back(objIndex(tok.substr(0, a), m.v.size()));
                if (a != std::string::npos) {
                    size_t b = tok.find('/', a + 1);
                    std::string t = tok.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
                    if (!t.empty()) fuv.push_back(objIndex(t, m.uv.size()));
                }
            }
            if (face.size() >= 3) {
                m.f.push_back(face);
                m.fuv.push_back(fuv.size() == face.size() ? fuv : std::vector<int>());
            }
        }
    }
    return !m.v.empty() && !m.f.empty();
}

// Uncompressed 8/24/32-bit BMP -> RGB, first row = bottom row (OBJ "vt" convention).
static bool loadBMP(const std::string& path, std::vector<unsigned char>& rgb, int& w, int& h) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') return false;
    auto u16 = [&](size_t o) { return (uint32_t)(d[o] | (d[o + 1] << 8)); };
    auto u32 = [&](size_t o) { return (uint32_t)(d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | ((uint32_t)d[o + 3] << 24)); };
    uint32_t off = u32(10), dib = u32(14), comp = u32(30);
    int32_t W = (int32_t)u32(18), H = (int32_t)u32(22);
    int bpp = (int)u16(28);
    if (dib < 40 || (comp != 0 && comp != 3) || W <= 0 || H == 0) return false;
    if (bpp != 8 && bpp != 24 && bpp != 32) return false;
    bool topDown = H < 0;
    int hh = std::abs(H);
    size_t rowSize = ((size_t)W * bpp + 31) / 32 * 4;
    if (off + rowSize * hh > d.size()) return false;

    size_t palOff = 14 + dib;
    uint32_t palCount = bpp == 8 ? (u32(46) ? u32(46) : 256) : 0;
    if (bpp == 8 && palOff + palCount * 4 > d.size()) return false;

    w = W; h = hh;
    rgb.assign((size_t)w * h * 3, 0);
    for (int y = 0; y < hh; ++y) {
        const unsigned char* row = &d[off + rowSize * (topDown ? (hh - 1 - y) : y)];
        for (int x = 0; x < W; ++x) {
            unsigned char b, g, r;
            if (bpp == 8) {
                uint32_t i = row[x];
                if (i >= palCount) i = 0;
                b = d[palOff + i * 4]; g = d[palOff + i * 4 + 1]; r = d[palOff + i * 4 + 2];
            } else {
                int s = bpp / 8;
                b = row[x * s]; g = row[x * s + 1]; r = row[x * s + 2];
            }
            unsigned char* o = &rgb[((size_t)y * w + x) * 3];
            o[0] = r; o[1] = g; o[2] = b;
        }
    }
    return true;
}

static void addVariants(std::vector<std::string>& c, const std::string& p) {
    c.push_back(p);
    size_t k = p.find_last_of('.');
    std::string stem = k == std::string::npos ? p : p.substr(0, k);
    c.push_back(stem + ".bmp");
    c.push_back(stem + ".BMP");
}

// Texture path from the .mtl (map_Kd), falling back to <objname>.bmp / .BMP.
static std::string findTexturePath(const Mesh& m) {
    std::string dir = dirOf(m.path);
    std::vector<std::string> cand;
    if (!m.mtllib.empty()) {
        std::ifstream mtl(dir + m.mtllib);
        std::string line;
        while (std::getline(mtl, line)) {
            line = trim(line);
            if (line.rfind("map_Kd", 0) == 0) {
                std::string name = baseOf(trim(line.substr(6)));
                if (!name.empty()) addVariants(cand, dir + name);
                break;
            }
        }
    }
    addVariants(cand, dir + stemOf(m.path) + ".bmp");
    for (auto& c : cand) if (fileExists(c)) return c;
    return "";
}

// Needs a current GL context.
static void loadTexture(Mesh& m) {
    std::string tp = findTexturePath(m);
    std::vector<unsigned char> rgb;
    int w = 0, h = 0;
    if (tp.empty() || !loadBMP(tp, rgb, w, h)) {
        std::cerr << "No usable BMP texture for " << m.path << " (drawing untextured)\n";
        return;
    }
    glGenTextures(1, &m.tex);
    glBindTexture(GL_TEXTURE_2D, m.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    m.hasTex = true;
    std::cout << "Texture " << tp << " (" << w << "x" << h << ")\n";
}

// ---------------------------------------------------------------- geometry
// Center and scale the body mesh (or joints if no mesh) to a unit sphere; the SAME
// transform is applied to joints, body and eyes so they stay aligned.
static void normalize() {
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    auto acc = [&](float x, float y, float z) {
        float p[3] = {x, y, z};
        for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], p[k]); mx[k] = std::max(mx[k], p[k]); }
    };
    if (!body.v.empty()) for (auto& v : body.v) acc(v.x, v.y, v.z);
    else for (auto& j : joints) acc(j.x, j.y, j.z);

    float c[3], r = 1e-6f;
    for (int k = 0; k < 3; ++k) { c[k] = 0.5f * (mn[k] + mx[k]); r = std::max(r, 0.5f * (mx[k] - mn[k])); }
    for (auto& j : joints) { j.x = (j.x - c[0]) / r; j.y = (j.y - c[1]) / r; j.z = (j.z - c[2]) / r; }
    for (auto& v : body.v) { v.x = (v.x - c[0]) / r; v.y = (v.y - c[1]) / r; v.z = (v.z - c[2]) / r; }
    for (auto& v : eyes.v) { v.x = (v.x - c[0]) / r; v.y = (v.y - c[1]) / r; v.z = (v.z - c[2]) / r; }
}

// Flat per-face normals (Newell's method, works for quads and n-gons).
static void computeNormals(Mesh& m) {
    m.fn.resize(m.f.size());
    for (size_t f = 0; f < m.f.size(); ++f) {
        Vec3 n = {0, 0, 0};
        const auto& fc = m.f[f];
        for (size_t i = 0; i < fc.size(); ++i) {
            const Vec3& a = m.v[fc[i]];
            const Vec3& b = m.v[fc[(i + 1) % fc.size()]];
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
        }
        float l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (l > 0) { n.x /= l; n.y /= l; n.z /= l; }
        m.fn[f] = n;
    }
}

// ---------------------------------------------------------------- input
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
    if (key == GLFW_KEY_E) showEyes = !showEyes;
    if (key == GLFW_KEY_T) useTexture = !useTexture;
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

// ---------------------------------------------------------------- drawing
static void drawMesh(const Mesh& m) {
    bool textured = useTexture && m.hasTex && !wireframe;
    if (wireframe) {
        glDisable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glColor3f(0.55f, 0.65f, 0.8f);
    } else {
        glEnable(GL_LIGHTING);
        glEnable(GL_COLOR_MATERIAL);
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if (textured) glColor3f(1.f, 1.f, 1.f); else glColor3f(0.75f, 0.78f, 0.85f);
    }
    if (textured) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, m.tex);
    }
    for (size_t f = 0; f < m.f.size(); ++f) {
        bool hasUV = textured && !m.fuv[f].empty();
        glBegin(GL_POLYGON);   // faces from RaBit are convex quads
        glNormal3f(m.fn[f].x, m.fn[f].y, m.fn[f].z);
        for (size_t k = 0; k < m.f[f].size(); ++k) {
            if (hasUV) {
                const Vec2& t = m.uv[m.fuv[f][k]];
                glTexCoord2f(t.x, t.y);
            }
            const Vec3& p = m.v[m.f[f][k]];
            glVertex3f(p.x, p.y, p.z);
        }
        glEnd();
    }
    if (textured) glDisable(GL_TEXTURE_2D);
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
    if (pos.empty()) {
        std::cerr << "Usage: " << argv[0] << " <id> [--shot out.bmp]\n";
        return 1;
    }
    const std::string path = "assets/RaBit/" + pos[0] + "/3d_model";
    const std::string jointsPath = path + "/3d_joints.txt";
    const std::string meshPath = path + "/m.obj";
    const std::string eyesPath = path + "/e.obj";
    if (!loadJoints(jointsPath)) {
        std::cerr << "Could not load joints: " << jointsPath << "\n";
        return 1;
    }
    if (!loadObj(meshPath, body)) {
        std::cerr << "Could not load mesh: " << meshPath << "\n";
        return 1;
    }
    hasEyes = loadObj(eyesPath, eyes);
    if (!hasEyes) std::cerr << "No eyes mesh loaded (" << eyesPath << "), continuing without eyes\n";
    normalize();
    computeNormals(body);
    if (hasEyes) computeNormals(eyes);

    if (!glfwInit()) return 1;
    GLFWwindow* win = glfwCreateWindow(900, 900, "RaBit mesh + joints", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSetMouseButtonCallback(win, onMouseButton);
    glfwSetCursorPosCallback(win, onCursor);
    glfwSetScrollCallback(win, onScroll);
    glfwSetKeyCallback(win, onKey);

    loadTexture(body);              // textures need the GL context
    if (hasEyes) loadTexture(eyes);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POINT_SMOOTH);
    glEnable(GL_NORMALIZE);
    const GLfloat amb[4] = {0.35f, 0.35f, 0.35f, 1.f}, dif[4] = {0.8f, 0.8f, 0.8f, 1.f};
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);

    while (!glfwWindowShouldClose(win)) {
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.f);
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

        if (showMesh) {
            drawMesh(body);
            if (hasEyes && showEyes) drawMesh(eyes);
        }
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