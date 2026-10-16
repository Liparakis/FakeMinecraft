#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

/**
 * @enum Camera_Movement
 * @brief Defines the cardinal directions of camera translation.
 */
enum Camera_Movement {
	FORWARD,
	BACKWARD,
	LEFT,
	RIGHT,
	UP,
	DOWN
};

/**
 * @name Default Camera Values
 * Default settings for initialization.
 * @{
 */
constexpr float YAW = -90.0f;
constexpr float PITCH = 0.0f;
constexpr float SPEED = 5.0f;
constexpr float SENSITIVITY = 0.1f;
constexpr float ZOOM = 70.0f;
/** @} */

/**
 * @name Euler Constraints
 * Limits and wrap constants for rotation.
 * @{
 */
constexpr float PITCH_MAX = 89.0f;  ///< Preventing gimbal lock by stopping at 89 degrees.
constexpr float PITCH_MIN = -89.0f; ///< Preventing gimbal lock.
constexpr float YAW_WRAP = 360.0f;  ///< Wrap value for modular arithmetic on yaw.
/** @} */

/**
 * @class Camera
 * @brief Handles 3D view and projection logic, translating input into world-space vectors.
 * 
 * The Camera class uses Euler angles (Pitch and Yaw) to calculate the Front, Right, and Up 
 * vectors required for lookAt matrix generation. It supports both free-floating movement 
 * (Freecam) and standard first-person controls.
 * 
 * Frequently used variables are kept together to help the CPU load them
 * more efficiently.
 */
class Camera
{
public:
	/** @name Core Vectors
	 * Variables used every frame for movement and looking.
	 * @{
	 */
	glm::vec3 Position;      ///< Current world-space position.
	glm::vec3 Front;         ///< Direction the camera is looking.
	glm::vec3 Right;         ///< Normalized vector pointing to the camera's right.
	glm::vec3 Up;            ///< Normalized vector pointing to the camera's relative up.
	/** @} */

	/** @name Extra Data
	 * Settings and less frequent variables.
	 * @{
	 */
	glm::vec3 WorldUp;       ///< Global up definition (typically Y+).
	float Yaw;               ///< Horizontal rotation (Degrees).
	float Pitch;             ///< Vertical rotation (Degrees).
	float MouseSensitivity;  ///< Multiplier for delta-mouse input.
	/** @} */

	/**
	 * @brief Constructs a camera using direct vector inputs.
	 */
	explicit Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f),
				   glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
				   float yaw = YAW,
				   float pitch = PITCH);

	/**
	 * @brief Constructs a camera using scalar floor coordinates.
	 */
	Camera(float posX, float posY, float posZ,
		  float upX, float upY, float upZ,
		  float yaw, float pitch);

	/**
	 * @brief Generates the View Matrix for a LookAt shader uniform.
	 * @return A 4x4 Transformation Matrix.
	 */
	[[nodiscard]] glm::mat4 getViewMatrix() const;

	/**
	 * @brief Processes discrete movement commands.
	 * @param direction Cardinal direction.
	 * @param deltaTime Seconds since last frame, used for velocity scaling.
	 */
	void processKeyboard(Camera_Movement direction, float deltaTime);

	/**
	 * @brief Processes analog mouse delta input to update rotation.
	 * @param xoffset Delta movement in X.
	 * @param yoffset Delta movement in Y.
	 * @param constrainPitch Whether to clamp vertical rotation to prevent flipping.
	 */
	void processMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

private:
	/**
	 * @brief Recalculates Front, Right, and Up vectors from the current Euler angles.
	 * 
	 * This method performs the trigonometric transformation:
	 * Front = (cos(yaw)*cos(pitch), sin(pitch), sin(yaw)*cos(pitch))
	 */
	void updateCameraVectors();
};