#pragma once
#include <Eigen/Dense>
#include "glm/ext/vector_float3.hpp"
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <vector>

struct Bone {
  int r; // index of root joint
  int t; // index of tip joint
  double w = 1.0;
};


class CameraRecoveryEngine {
public:
  CameraRecoveryEngine();
  ~CameraRecoveryEngine();
  Eigen::Matrix<double, 3, 4> RecoverCamera(const std::vector<glm::vec2> imagePoints, const std::vector<glm::vec3> modelPoints, const std::vector<Bone> bones);
};
