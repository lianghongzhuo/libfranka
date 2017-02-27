#pragma once

#include <research_interface/rbk_types.h>
#include <franka/robot.h>

namespace franka {

class MotionGenerator {
public:
  MotionGenerator() = delete;
  MotionGenerator(const MotionGenerator &) = delete;
  MotionGenerator(const MotionGenerator&& motion_generator);
  MotionGenerator(Robot::Impl& robot);
  ~MotionGenerator() {
    // motion_generator_running_ = false;
  }
private:
  research_interface::RobotCommand& robot_command_;
  bool& motion_generator_running_;
};


class CartesianPoseMotionGenerator : public MotionGenerator {
public:
  CartesianPoseMotionGenerator() = delete;
  CartesianPoseMotionGenerator(const CartesianPoseMotionGenerator&) = delete;
  using MotionGenerator::MotionGenerator;
  void setDesiredPose(std::array<double, 7>& desired_pose);
  bool checkPose(std::array<double, 7>& pose);
};


class CartesianVelocityMotionGenerator : public MotionGenerator {
public:
  CartesianVelocityMotionGenerator() = delete;
  CartesianVelocityMotionGenerator(const CartesianVelocityMotionGenerator&) = delete;
  using MotionGenerator::MotionGenerator;
  void setDesiredVelocity(std::array<double, 6>& desired_velocity);
};


class JointPoseMotionGenerator : public MotionGenerator {
public:
  JointPoseMotionGenerator() = delete;
  JointPoseMotionGenerator(const JointPoseMotionGenerator&) = delete;
  using MotionGenerator::MotionGenerator;
  void setDesiredPose(std::array<double, 7>& desired_pose);
  bool checkPose(std::array<double, 7>& desired_pose);
};


class JointVelocityMotionGenerator : public MotionGenerator {
public:
  JointVelocityMotionGenerator() = delete;
  JointVelocityMotionGenerator(const JointVelocityMotionGenerator&) = delete;
  using MotionGenerator::MotionGenerator;
  void setDesiredVelocity(std::array<double, 7>& desired_velocity);
};

}  // namespace franka
