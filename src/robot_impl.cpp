#include "robot_impl.h"

#include <cstring>
#include <sstream>

// `using std::string_literals::operator""s` produces a GCC warning that cannot
// be disabled, so we have to use `using namespace ...`.
// See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=65923#c0
using namespace std::string_literals;  // NOLINT (google-build-using-namespace)

namespace franka {

constexpr std::chrono::seconds Robot::Impl::kDefaultTimeout;

Robot::Impl::Impl(const std::string& franka_address,
                  uint16_t franka_port,
                  std::chrono::milliseconds timeout,
                  RealtimeConfig realtime_config)
    : realtime_config_{realtime_config},
      network_{franka_address, franka_port, timeout},
      motion_generator_running_{false},
      controller_running_{false} {}

bool Robot::Impl::update() {
  if (!network_.handleReplies(*this)) {
    return false;  // server sent EOF
  }

  robot_state_ = network_.udpReadRobotState();

  if (motion_generator_running_ || controller_running_) {
    robot_command_.message_id = robot_state_.rcuRobotState().message_id;

    int bytes_sent = network_.udpSendRobotCommand(robot_command_);
    if (bytes_sent != sizeof(robot_command_)) {
      throw NetworkException("libfranka: robot command send error");
    }
  }
  return true;
}

const RobotState& Robot::Impl::robotState() const noexcept {
  return robot_state_.robotState();
}

Robot::ServerVersion Robot::Impl::serverVersion() const noexcept {
  return network_.serverVersion();
}

bool Robot::Impl::motionGeneratorRunning() const noexcept {
  return motion_generator_running_;
}

bool Robot::Impl::controllerRunning() const noexcept {
  return controller_running_;
}

RealtimeConfig Robot::Impl::realtimeConfig() const noexcept {
  return realtime_config_;
}

void Robot::Impl::controllerCommand(
    const research_interface::ControllerCommand& controller_command) noexcept {
  robot_command_.control = controller_command;
}

void Robot::Impl::motionGeneratorCommand(
    const research_interface::MotionGeneratorCommand&
        motion_generator_command) noexcept {
  robot_command_.motion = motion_generator_command;
}

void Robot::Impl::startMotionGenerator(
    research_interface::StartMotionGeneratorRequest::Type
        motion_generator_type) {
  if (motion_generator_running_) {
    throw ControlException(
        "libfranka: attempted to start multiple motion generators!");
  }
  std::memset(&robot_command_, 0, sizeof(robot_command_));

  research_interface::StartMotionGeneratorRequest request(
      motion_generator_type);
  network_.tcpSendRequest(request);

  network_.expectReply(research_interface::Function::kStartMotionGenerator);

  research_interface::MotionGeneratorMode motion_generator_mode;
  switch (motion_generator_type) {
    case decltype(motion_generator_type)::kJointPosition:
      motion_generator_mode = decltype(motion_generator_mode)::kJointPosition;
      break;
    case decltype(motion_generator_type)::kJointVelocity:
      motion_generator_mode = decltype(motion_generator_mode)::kJointVelocity;
      break;
    case decltype(motion_generator_type)::kCartesianPosition:
      motion_generator_mode =
          decltype(motion_generator_mode)::kCartesianPosition;
      break;
    case decltype(motion_generator_type)::kCartesianVelocity:
      motion_generator_mode =
          decltype(motion_generator_mode)::kCartesianVelocity;
      break;
    default:
      throw std::runtime_error(
          "No matching research_interface::MotionGeneratorMode for the "
          "research_interface::StartMotionGeneratorRequest::Type");
  }

  while (update()) {
    if (robot_state_.rcuRobotState().motion_generator_mode ==
        motion_generator_mode) {
      motion_generator_running_ = true;
      return;
    }
  }
  throw NetworkException("libfranka: connection closed by server.");
}

void Robot::Impl::stopMotionGenerator() {
  if (!motion_generator_running_) {
    return;
  }

  research_interface::StopMotionGeneratorRequest request;
  network_.tcpSendRequest(request);

  network_.expectReply(research_interface::Function::kStopMotionGenerator);

  robot_command_.motion.motion_generation_finished = true;
  while (update()) {
    if (robot_state_.rcuRobotState().motion_generator_mode ==
        research_interface::MotionGeneratorMode::kIdle) {
      motion_generator_running_ = false;
      return;
    }
  }
  throw NetworkException("libfranka: connection closed by server.");
}

void Robot::Impl::startController() {
  if (controller_running_) {
    throw ControlException(
        "libfranka: attempted to start multiple controllers!");
  }

  research_interface::StartControllerRequest request;
  network_.tcpSendRequest(request);

  network_.expectReply(research_interface::Function::kStartController);

  while (update()) {
    if (robot_state_.rcuRobotState().controller_mode ==
        research_interface::ControllerMode::kExternalController) {
      controller_running_ = true;
      return;
    }
  }
  throw NetworkException("libfranka: connection closed by server.");
}

void Robot::Impl::stopController() {
  if (!controller_running_) {
    return;
  }

  research_interface::StopControllerRequest request;
  network_.tcpSendRequest(request);

  network_.expectReply(research_interface::Function::kStopController);

  while (update()) {
    if (robot_state_.rcuRobotState().controller_mode !=
        research_interface::ControllerMode::kExternalController) {
      controller_running_ = false;
      return;
    }
  }
  throw NetworkException("libfranka: connection closed by server.");
}

void Robot::Impl::handleStartMotionGeneratorReply(
    const research_interface::StartMotionGeneratorReply& reply) {
  switch (reply.status) {
    case research_interface::StartMotionGeneratorReply::Status::kSuccess:
      // After sending Success, RCU will send another reply in the future,
      // e.g. Finished or Aborted.
      network_.expectReply(research_interface::Function::kStartMotionGenerator);
      break;
    case research_interface::StartMotionGeneratorReply::Status::kFinished:
      motion_generator_running_ = false;
      break;
    case research_interface::StartMotionGeneratorReply::Status::kAborted:
      motion_generator_running_ = false;
      throw ControlException("libfranka: motion generator command aborted!");
    case research_interface::StartMotionGeneratorReply::Status::kRejected:
      motion_generator_running_ = false;
      throw ControlException("libfranka: motion generator command rejected!");
    default:
      motion_generator_running_ = false;
      throw ProtocolException(
          "libfranka: unexpected start motion generator reply!");
  }
}

void Robot::Impl::handleStopMotionGeneratorReply(
    const research_interface::StopMotionGeneratorReply& reply) {
  if (reply.status !=
      research_interface::StopMotionGeneratorReply::Status::kSuccess) {
    throw ProtocolException(
        "libfranka: unexpected stop motion generator reply!");
  }
}

void Robot::Impl::handleStartControllerReply(
    const research_interface::StartControllerReply& reply) {
  switch (reply.status) {
    case research_interface::StartControllerReply::Status::kSuccess:
      break;
    default:
      controller_running_ = false;
      throw ProtocolException("libfranka: unexpected start controller reply!");
  }
}

void Robot::Impl::handleStopControllerReply(
    const research_interface::StopControllerReply& reply) {
  if (reply.status !=
      research_interface::StopControllerReply::Status::kSuccess) {
    throw ProtocolException(
        "libfranka: unexpected stop motion generator reply!");
  }
}

}  // namespace franka
