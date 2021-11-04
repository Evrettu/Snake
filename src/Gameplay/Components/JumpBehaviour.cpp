#include "Gameplay/Components/JumpBehaviour.h"
#include <GLFW/glfw3.h>
#include "Gameplay/GameObject.h"
#include "Gameplay/Scene.h"
#include "Utils/ImGuiHelper.h"

void JumpBehaviour::Awake()
{
	_body = GetComponent<Gameplay::Physics::RigidBody>();
	if (_body == nullptr) {
		IsEnabled = false;
	}
}

void JumpBehaviour::RenderImGui() {
	LABEL_LEFT(ImGui::DragFloat, "Impulse", &_impulse, 0.5f);
}

nlohmann::json JumpBehaviour::ToJson() const {
	return {
		{ "impulse", _impulse }
	};
}

JumpBehaviour::JumpBehaviour() :
	IComponent(),
	_impulse(0.5f)
{ }

JumpBehaviour::~JumpBehaviour() = default;

JumpBehaviour::Sptr JumpBehaviour::FromJson(const nlohmann::json& blob) {
	JumpBehaviour::Sptr result = std::make_shared<JumpBehaviour>();
	result->_impulse = blob["impulse"];
	return result;
}

void JumpBehaviour::Update(float deltaTime) {
	bool pressed = glfwGetKey(GetGameObject()->GetScene()->Window, GLFW_KEY_W);
	if (pressed) {
		if (_isPressed == false) 
		{
			_body->ApplyImpulse(glm::vec3(0.0f, _impulse, 0.0f));
		}
		_isPressed = pressed;
	} else {
		_isPressed = false;
	}

	bool pressed1 = glfwGetKey(GetGameObject()->GetScene()->Window, GLFW_KEY_S);
	if (pressed1) {
		if (_isPressed == false) {
			_body->ApplyImpulse(glm::vec3(0.0f, _impulse * -1.0f, 0.0f));
		}
		_isPressed = pressed1;
	}
	else {
		_isPressed = false;
	}

	bool pressed2 = glfwGetKey(GetGameObject()->GetScene()->Window, GLFW_KEY_A);
	if (pressed2) {
		if (_isPressed == false) {
			_body->ApplyImpulse(glm::vec3(_impulse * -1.0f, 0.0f, 0.0f));
		}
		_isPressed = pressed2;
	}
	else {
		_isPressed = false;
	}

	bool pressed3 = glfwGetKey(GetGameObject()->GetScene()->Window, GLFW_KEY_D);
	if (pressed3) {
		if (_isPressed == false) {
			_body->ApplyImpulse(glm::vec3(_impulse, 0.0f, 0.0f));
		}
		_isPressed = pressed3;
	}
	else {
		_isPressed = false;
	}

}

