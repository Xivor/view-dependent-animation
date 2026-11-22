/**
 * @file Camera.h
 * @brief Perspective camera producing view and projection matrices.
 */
#pragma once
#include <glm/glm.hpp>
#include <vector>

struct CameraImageProjection {
    glm::mat4 viewMatrix{1.f};
    glm::mat4 projectionMatrix{1.f};
    glm::ivec2 framebufferSize{0};
    glm::ivec2 cropOrigin{0};
    glm::ivec2 imageSize{512};
    glm::vec3 target{0.f};
    std::vector<float> depthValues;

    /**
     * @brief Unprojects a top-left-origin image pixel to its visible world-space surface point.
     * @param pixel Pixel coordinates in the captured image, from (0, 0) to (511, 511).
     */
    glm::vec3 backToSpace(const glm::vec2& pixel) const;
    glm::vec3 closestProjection(const glm::vec2& pixel, const glm::vec3& worldPoint) const;
};

/**
 * @brief A perspective camera defined by position, target, and field of view.
 */
struct Camera {
    glm::vec3 position  {0.f, 1.f, 5.f};
    glm::vec3 target    {0.f, 0.f, 0.f};
    glm::vec3 up        {0.f, 1.f, 0.f};
    float     fov       = 45.f;  ///< Vertical field of view in degrees.
    float     nearPlane = 0.1f;
    float     farPlane  = 100.f; //100.f

    /**
     * @brief Computes the view matrix (world → camera space).
     * @return Column-major view matrix.
     */
    glm::mat4 view() const;

    /**
     * @brief Computes the projection matrix (camera → clip space).
     * @param aspect Viewport width / height.
     * @return Column-major perspective projection matrix.
     */
    glm::mat4 projection(float aspect) const;
};
