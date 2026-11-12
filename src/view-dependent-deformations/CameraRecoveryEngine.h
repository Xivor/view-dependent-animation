#include "glm/ext/matrix_float3x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <vector>


class CameraRecoveryEngine {
public:
  CameraRecoveryEngine();
  ~CameraRecoveryEngine();
  glm::mat3x4 RecoverCamera(const std::vector<glm::vec2> imagePoints, const std::vector<glm::vec3> modelPoints);
};
