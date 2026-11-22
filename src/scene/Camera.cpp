#include "scene/Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>

glm::vec3 CameraImageProjection::backToSpace(const glm::vec2& pixel) const {
    if (framebufferSize.x <= 0 || framebufferSize.y <= 0 ||
        imageSize.x <= 0 || imageSize.y <= 0 ||
        pixel.x < 0.f || pixel.y < 0.f ||
        pixel.x >= imageSize.x || pixel.y >= imageSize.y ||
        depthValues.size() != static_cast<size_t>(imageSize.x * imageSize.y)) {
        return target;
    }

    const int pixelX = static_cast<int>(pixel.x);
    const int pixelY = static_cast<int>(pixel.y);
    const size_t depthIndex = static_cast<size_t>(imageSize.y - 1 - pixelY) * imageSize.x + pixelX;
    const float depth = depthValues[depthIndex];
    const float framebufferX = cropOrigin.x + pixel.x + 0.5f;
    const float framebufferY = cropOrigin.y + imageSize.y - pixel.y - 0.5f;
    const float ndcX = 2.f * framebufferX / framebufferSize.x - 1.f;
    const float ndcY = 2.f * framebufferY / framebufferSize.y - 1.f;
    const float ndcZ = 2.f * depth - 1.f;
    const glm::mat4 inverseViewProjection = glm::inverse(projectionMatrix * viewMatrix);

    glm::vec4 worldPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, ndcZ, 1.f);
    if (std::abs(worldPoint.w) < 1e-6f) return target;
    return glm::vec3(worldPoint / worldPoint.w);
}

glm::vec3 CameraImageProjection::closestProjection(const glm::vec2& pixel, const glm::vec3& worldPoint) const {
    if (framebufferSize.x <= 0 || framebufferSize.y <= 0) return worldPoint;
 
    // Same pixel -> NDC mapping as backToSpace (top-left image origin, crop inside the framebuffer).
    const float framebufferX = cropOrigin.x + pixel.x + 0.5f;
    const float framebufferY = cropOrigin.y + imageSize.y - pixel.y - 0.5f;
    const float ndcX = 2.f * framebufferX / framebufferSize.x - 1.f;
    const float ndcY = 2.f * framebufferY / framebufferSize.y - 1.f;
 
    // The pixel is a point on the image plane, not in world space: unproject it through the camera.
    // The points at NDC depth -1 and +1 lie on the same viewing ray (which passes through the camera center).
    const glm::mat4 inverseViewProjection = glm::inverse(projectionMatrix * viewMatrix);
    const glm::vec4 nearH = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.f, 1.f);
    const glm::vec4 farH  = inverseViewProjection * glm::vec4(ndcX, ndcY,  1.f, 1.f);
    if (std::abs(nearH.w) < 1e-6f || std::abs(farH.w) < 1e-6f) return worldPoint;
 
    const glm::vec3 cameraCenter = glm::vec3(glm::inverse(viewMatrix) * glm::vec4(0.f, 0.f, 0.f, 1.f));
    const glm::vec3 rayDirection = glm::normalize(glm::vec3(farH) / farH.w - glm::vec3(nearH) / nearH.w);
 
    // Point of the ray closest to worldPoint (same depth along the ray as worldPoint).
    return cameraCenter + rayDirection * glm::dot(worldPoint - cameraCenter, rayDirection);
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position, target, up);
}

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}
