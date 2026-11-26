#include "CameraRecoveryEngine.h"
#include <iostream>
#include <glm/ext/matrix_transform.hpp>
#include <cmath>
#include <numeric>

Eigen::Vector2d toEigen(const glm::vec2& v) {
    return Eigen::Vector2d(
        static_cast<double>(v.x),
        static_cast<double>(v.y)
    );
}

Eigen::Vector3d toEigen(const glm::vec3& v) {
    return Eigen::Vector3d(
        static_cast<double>(v.x),
        static_cast<double>(v.y),
        static_cast<double>(v.z)
    );
}

template <typename T> T average(std::vector<T> v) {
  if (v.empty()) return T{};
  return std::reduce(v.cbegin(), v.cend(), T{}) / static_cast<float>(v.size());
}

Eigen::Matrix<double, 3, 4> solveDLT(const std::vector<Eigen::Vector2d> normImagePoints, const std::vector<Eigen::Vector3d> normModelPoints, const std::vector<Bone> bones) {
    const int n = static_cast<int>(normImagePoints.size());
    std::vector<Eigen::Matrix<double, 1, 12>> boneRows;
    for (const auto& bone : bones) {

        const int r = bone.r;
        const int t = bone.t;

        if (r < 0 || t < 0 ||
            r >= n || t >= n) {
            continue;
        }

        if (bone.w <= 0.0)
            continue;
        Eigen::Vector3d xr(
            normImagePoints[r].x(),
            normImagePoints[r].y(),
            1.0
        );

        Eigen::Vector3d xt(
            normImagePoints[t].x(),
            normImagePoints[t].y(),
            1.0
        );
        Eigen::Vector3d l = xr.cross(xt);
        double lineNorm = std::hypot(l(0), l(1));
        if (lineNorm < 1e-10)
            continue;
        l /= lineNorm;

        Eigen::Vector3d D = normModelPoints[t] - normModelPoints[r];
        double boneLength = D.norm();
        if (boneLength < 1e-10)
            continue;
        D /= boneLength;
        Eigen::Matrix<double, 1, 12> boneRow;
        boneRow <<
            l(0) * D(0), l(0) * D(1), l(0) * D(2), 0.0,
            l(1) * D(0), l(1) * D(1), l(1) * D(2), 0.0,
            l(2) * D(0), l(2) * D(1), l(2) * D(2), 0.0;

        boneRow *= bone.w;
        boneRows.push_back(boneRow);
    }
    const int numBones = static_cast<int>(boneRows.size());
    Eigen::MatrixXd A(2 * n + numBones, 12);
    A.setZero();
    for (int i = 0; i < n; i++) {
        double X = normModelPoints[i].x();
        double Y = normModelPoints[i].y();
        double Z = normModelPoints[i].z();
        double x = normImagePoints[i].x();
        double y = normImagePoints[i].y();

        // y constraint
        A.row(2 * i) <<
            0, 0, 0, 0,
            -X, -Y, -Z, -1,
            y * X, y * Y, y * Z, y;

        // x constraint
        A.row(2 * i + 1) <<
            X, Y, Z, 1,
            0, 0, 0, 0,
            -x * X, -x * Y, -x * Z, -x;
    }
    for (int i = 0; i < numBones; i++) {
        A.row(2 * n + i) = boneRows[i];
    }

    Eigen::JacobiSVD<Eigen::MatrixXd> svd(
        A, Eigen::ComputeFullV
    );
    Eigen::VectorXd h = svd.matrixV().col(11);
    return Eigen::Map<
        const Eigen::Matrix<double, 3, 4, Eigen::RowMajor>
    >(h.data());
}

// CLASS FUNCTIONS
CameraRecoveryEngine::CameraRecoveryEngine() {}

CameraRecoveryEngine::~CameraRecoveryEngine() {}

Eigen::Matrix<double, 3, 4> CameraRecoveryEngine::RecoverCamera(const std::vector<glm::vec2> imagePoints, const std::vector<glm::vec3> modelPoints, const std::vector<Bone> bones) {
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

  std::vector<Eigen::Vector2d> normImagePointsEigen;
  std::vector<Eigen::Vector3d> normModelPointsEigen;

  for (const auto& p : normImagePoints) {
    normImagePointsEigen.push_back(toEigen(p));
  }
  for (const auto& p : normModelPoints) {
    normModelPointsEigen.push_back(toEigen(p));
  }

  // 2. for each point correspondence: calculate a matrix A_i for then to put them all in a 2n x 3 matrix A
  // 3. take svd of A, and smallest value is least square solution for P'
  Eigen::Matrix<double, 3, 4> P_transformed = solveDLT(normImagePointsEigen, normModelPointsEigen, bones);

  // 4. P = T^-1P'U
  Eigen::Matrix<double, 3, 4> P = T2d_inv * P_transformed * T3d;
  // glm::mat3x4 P_glm;
  // for (int col = 0; col < 3; ++col) {
  //   for (int row = 0; row < 4; ++row) {
  //     P_glm[col][row] = static_cast<float>(P(row, col));
  //   }
  // }
  // DEBUG
  double squaredError = 0.0;

  for (size_t i = 0; i < modelPoints.size(); ++i) {
    Eigen::Vector4d X(
      modelPoints[i].x,
      modelPoints[i].y,
      modelPoints[i].z,
      1.0
    );

    Eigen::Vector3d projected = P * X;

    if (std::abs(projected(2)) < 1e-10)
      continue;

    double x = projected(0) / projected(2);
    double y = projected(1) / projected(2);

    double dx = x - imagePoints[i].x;
    double dy = y - imagePoints[i].y;

    squaredError += dx * dx + dy * dy;
  }

  double rmse = std::sqrt(
    squaredError / modelPoints.size()
  );

  std::cout << "Reprojection RMSE: " << rmse << " pixels\n";

  return P;

}
