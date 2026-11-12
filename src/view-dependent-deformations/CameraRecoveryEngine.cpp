#include "CameraRecoveryEngine.h"
#include <glm/ext/matrix_transform.hpp>
#include <cmath>
#include <numeric>
#include <Eigen/Dense>


template <typename T> T average(std::vector<T> v) {
  if (v.empty()) return T{};
  return std::reduce(v.cbegin(), v.cend(), T{}) / static_cast<float>(v.size());
}


Eigen::Matrix<double, 3, 4> solveDLT(const std::vector<glm::vec2> normImagePoints, const std::vector<glm::vec3> normModelPoints) {
  int n = normImagePoints.size();
  Eigen::MatrixXd A(2 * n, 12);
  A.setZero();

  for (int i = 0; i < n; i++) {
    double X = normModelPoints[i].x;
    double Y = normModelPoints[i].y;
    double Z = normModelPoints[i].z;
    double x = normImagePoints[i].x;
    double y = normImagePoints[i].y;

    A(2 * i, 4) = -X;
    A(2 * i, 5) = -Y;
    A(2 * i, 6) = -Z;
    A(2 * i, 7) = -1;
    A(2 * i, 8) = y * X;
    A(2 * i, 9) = y * Y;
    A(2 * i, 10) = y * Z;
    A(2 * i, 11) = y;
    A(2 * i + 1, 0) = X;
    A(2 * i + 1, 1) = Y;
    A(2 * i + 1, 2) = Z;
    A(2 * i + 1, 3) = 1;
    A(2 * i + 1, 8) = -x * X;
    A(2 * i + 1, 9) = -x * Y;
    A(2 * i + 1, 10) = -x * Z;
    A(2 * i + 1, 11) = -x;
  }
  Eigen::JacobiSVD<Eigen::MatrixXd> svd(A, Eigen::ComputeFullV);
  Eigen::VectorXd h = svd.matrixV().col(11); // rightmost column is h
  return Eigen::Map<const Eigen::Matrix<double, 3, 4, Eigen::RowMajor>>(h.data());
}


// CLASS FUNCTIONS
CameraRecoveryEngine::CameraRecoveryEngine() {}

CameraRecoveryEngine::~CameraRecoveryEngine() {}

glm::mat3x4 CameraRecoveryEngine::RecoverCamera(const std::vector<glm::vec2> imagePoints, const std::vector<glm::vec3> modelPoints) {
  // 1. normalize 2d and 3d points
  glm::vec2 centroid2d = average(imagePoints);
  std::vector<glm::vec2> normImagePoints;
  float averageDist = 0.0f;
  for (auto &point : imagePoints) {
    glm::vec2 normPoint = point - centroid2d;
    averageDist += normPoint.length();
    normImagePoints.push_back(normPoint);
  }
  averageDist /= imagePoints.size();
  float scale = std::sqrt(2.0f) / averageDist;
  for (auto &point : normImagePoints) {
    point *= scale;
  }
  Eigen::Matrix3d T2d_inv = Eigen::Matrix3d::Identity();
  T2d_inv(0, 0) = 1.0 / scale;
  T2d_inv(1, 1) = 1.0 / scale;
  T2d_inv(0, 2) = centroid2d.x;
  T2d_inv(1, 2) = centroid2d.y;

  glm::vec3 centroid3d = average(modelPoints);
  std::vector<glm::vec3> normModelPoints;
  averageDist = 0;
  for (auto &point : modelPoints) {
    glm::vec3 normPoint = point - centroid3d;
    averageDist += normPoint.length();
    normModelPoints.push_back(normPoint);
  }
  averageDist /= modelPoints.size();
  scale = std::sqrt(3.0f) / averageDist;
  for (auto &point : normModelPoints) {
    point *= scale;
  }
  Eigen::Matrix4d T3d = Eigen::Matrix4d::Identity();
  T3d(0, 0) = scale;
  T3d(1, 1) = scale;
  T3d(2, 2) = scale;
  T3d(0, 3) = -scale * centroid3d.x;
  T3d(1, 3) = -scale * centroid3d.y;
  T3d(2, 3) = -scale * centroid3d.z;

  // 2. for each point correspondence: calculate a matrix A_i for then to put them all in a 2n x 3 matrix A
  // 3. take svd of A, and smallest value is least square solution for P'
  Eigen::Matrix<double, 3, 4> P_transformed = solveDLT(normImagePoints, normModelPoints);

  // 4. P = T^-1P'U
  Eigen::Matrix<double, 3, 4> P = T2d_inv * P_transformed * T3d;
  glm::mat3x4 P_glm;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 3; ++j) {
      P_glm[i][j] = static_cast<float>(P(j, i));
    }
  }
  return P_glm;
}
