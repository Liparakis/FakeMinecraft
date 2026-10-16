#include "Camera.h"
#include <cmath>

/**
 * @file Camera.cpp
 * @brief This file handles the camera and how we move around the world.
 */

namespace {
    /// Unit vector pointing down the negative Z-axis.
    const glm::vec3 INITIAL_FRONT(0.0f, 0.0f, -1.0f);

    /**
     * @brief fmod is used here to wrap the angle smoothly if it exceeds the limit.
     */
    [[gnu::always_inline]]
    float wrapAngle(float angle, float max) {
        angle = std::fmod(angle, max);
        return (angle < 0.0f) ? angle + max : angle;
    }

    /**
     * @brief A simple helper to clamp values between a min and max.
     */
    [[gnu::always_inline]]
    float clamp(float value, float min, float max) {
        return (value < min) ? min : ((value > max) ? max : value);
    }
}

/**
 * @brief Constructor that sets up the camera and makes sure the orientation is correct from the start.
 */
Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : Position(position)
    , Front(INITIAL_FRONT)
    , Right(1.0f, 0.0f, 0.0f)
    , Up(0.0f, 1.0f, 0.0f)
    , WorldUp(up)
    , Yaw(yaw)
    , Pitch(pitch)
    , MouseSensitivity(SENSITIVITY)
{
    updateCameraVectors();
}

/**
 * @brief Another constructor if we want to pass each coordinate individually.
 */
Camera::Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch)
    : Position(posX, posY, posZ)
    , Front(INITIAL_FRONT)
    , Right(1.0f, 0.0f, 0.0f)
    , Up(0.0f, 1.0f, 0.0f)
    , WorldUp(upX, upY, upZ)
    , Yaw(yaw)
    , Pitch(pitch)
    , MouseSensitivity(SENSITIVITY)
{
    updateCameraVectors();
}

/**
 * @brief The LookAt algorithm is used to generate the view matrix.
 * 
 * View matrix depends on position and looking direction.
 */
glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(Position, Position + Front, Up);
}

/**
 * @brief This handles moving the camera when we press keys on the keyboard.
 */
void Camera::processKeyboard(Camera_Movement direction, float deltaTime) {
    const float velocity = deltaTime;

    // A switch statement handles the different movement directions.
    switch (direction) {
        case FORWARD:
            Position += Front * velocity;
            break;
        case BACKWARD:
            Position -= Front * velocity;
            break;
        case LEFT:
            Position -= Right * velocity;
            break;
        case RIGHT:
            Position += Right * velocity;
            break;
        case UP:
            Position.y += velocity;
            break;
        case DOWN:
            Position.y -= velocity;
            break;
    }
}

/**
 * @brief orientation is updated based on mouse movement.
 * 
 * Pitch constraints are applied to prevent excessive upward or downward rotation.
 */
void Camera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
    xoffset *= MouseSensitivity;
    yoffset *= MouseSensitivity;

    Yaw += xoffset;
    Pitch += yoffset;

    if (constrainPitch) [[likely]] {
        Pitch = clamp(Pitch, PITCH_MIN, PITCH_MAX);
        // Wrapping the yaw so the numbers don't get way too big.
        Yaw = wrapAngle(Yaw, YAW_WRAP);
    }

    updateCameraVectors();
}

/**
 * @brief Yaw and pitch are converted into a direction vector.
 * 
 * Standard trigonometry for spherical coordinates is used.
 */
void Camera::updateCameraVectors() {
    // Cache radians conversion
    const float yawRad = glm::radians(Yaw);
    const float pitchRad = glm::radians(Pitch);

    // Cache trig results
    const float cosYaw = std::cos(yawRad);
    const float sinYaw = std::sin(yawRad);
    const float cosPitch = std::cos(pitchRad);
    const float sinPitch = std::sin(pitchRad);

    // Calculate the new Front vector
    Front.x = cosYaw * cosPitch;
    Front.y = sinPitch;
    Front.z = sinYaw * cosPitch;

    // Vectors are normalized to ensure consistent movement speed in every direction.
    Right = glm::normalize(glm::cross(Front, WorldUp));
    Up = glm::normalize(glm::cross(Right, Front));
}