#include <Logging.h>
#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <filesystem>
#include <json.hpp>
#include <fstream>
#include <sstream>
#include <typeindex>
#include <optional>
#include <string>

// GLM math library
#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>
#include <GLM/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <GLM/gtx/common.hpp> // for fmod (floating modulus)

// Graphics
#include "Graphics/IndexBuffer.h"
#include "Graphics/VertexBuffer.h"
#include "Graphics/VertexArrayObject.h"
#include "Graphics/Shader.h"
#include "Graphics/Texture2D.h"
#include "Graphics/VertexTypes.h"

// Utilities
#include "Utils/MeshBuilder.h"
#include "Utils/MeshFactory.h"
#include "Utils/ObjLoader.h"
#include "Utils/ImGuiHelper.h"
#include "Utils/ResourceManager/ResourceManager.h"
#include "Utils/FileHelpers.h"
#include "Utils/JsonGlmHelpers.h"
#include "Utils/StringUtils.h"
#include "Utils/GlmDefines.h"

// Gameplay
#include "Gameplay/Material.h"
#include "Gameplay/GameObject.h"
#include "Gameplay/Scene.h"

// Components
#include "Gameplay/Components/IComponent.h"
#include "Gameplay/Components/Camera.h"
#include "Gameplay/Components/RotatingBehaviour.h"
#include "Gameplay/Components/JumpBehaviour.h"
#include "Gameplay/Components/RenderComponent.h"
#include "Gameplay/Components/MaterialSwapBehaviour.h"

// Physics
#include "Gameplay/Physics/RigidBody.h"
#include "Gameplay/Physics/Colliders/BoxCollider.h"
#include "Gameplay/Physics/Colliders/PlaneCollider.h"
#include "Gameplay/Physics/Colliders/SphereCollider.h"
#include "Gameplay/Physics/Colliders/ConvexMeshCollider.h"
#include "Gameplay/Physics/TriggerVolume.h"
#include <gl/GL.h>

#include <GL/gl.h>
#include <gl/GLU.h>

int direction = 0;
bool foodinplay = false;
const int move_speed = 5;
int score = 0;
int worth = 0;

//#define LOG_GL_NOTIFICATIONS

/*
	Handles debug messages from OpenGL
	https://www.khronos.org/opengl/wiki/Debug_Output#Message_Components
	@param source    Which part of OpenGL dispatched the message
	@param type      The type of message (ex: error, performance issues, deprecated behavior)
	@param id        The ID of the error or message (to distinguish between different types of errors, like nullref or index out of range)
	@param severity  The severity of the message (from High to Notification)
	@param length    The length of the message
	@param message   The human readable message from OpenGL
	@param userParam The pointer we set with glDebugMessageCallback (should be the game pointer)
*/
void GlDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
	std::string sourceTxt;
	switch (source) {
		case GL_DEBUG_SOURCE_API: sourceTxt = "DEBUG"; break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM: sourceTxt = "WINDOW"; break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER: sourceTxt = "SHADER"; break;
		case GL_DEBUG_SOURCE_THIRD_PARTY: sourceTxt = "THIRD PARTY"; break;
		case GL_DEBUG_SOURCE_APPLICATION: sourceTxt = "APP"; break;
		case GL_DEBUG_SOURCE_OTHER: default: sourceTxt = "OTHER"; break;
	}
	switch (severity) {
		case GL_DEBUG_SEVERITY_LOW:          LOG_INFO("[{}] {}", sourceTxt, message); break;
		case GL_DEBUG_SEVERITY_MEDIUM:       LOG_WARN("[{}] {}", sourceTxt, message); break;
		case GL_DEBUG_SEVERITY_HIGH:         LOG_ERROR("[{}] {}", sourceTxt, message); break;
			#ifdef LOG_GL_NOTIFICATIONS
		case GL_DEBUG_SEVERITY_NOTIFICATION: LOG_INFO("[{}] {}", sourceTxt, message); break;
			#endif
		default: break;
	}
}

// Stores our GLFW window in a global variable for now
GLFWwindow* window;
// The current size of our window in pixels
glm::ivec2 windowSize = glm::ivec2(800, 800);
// The title of our GLFW window
std::string windowTitle = "Snake";

// using namespace should generally be avoided, and if used, make sure it's ONLY in cpp files
using namespace Gameplay;
using namespace Gameplay::Physics;

// The scene that we will be rendering
Scene::Sptr scene = nullptr;

void GlfwWindowResizedCallback(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
	windowSize = glm::ivec2(width, height);
	if (windowSize.x * windowSize.y > 0) {
		scene->MainCamera->ResizeWindow(width, height);
	}
}

/// <summary>
/// Handles intializing GLFW, should be called before initGLAD, but after Logger::Init()
/// Also handles creating the GLFW window
/// </summary>
/// <returns>True if GLFW was initialized, false if otherwise</returns>
bool initGLFW() {
	// Initialize GLFW
	if (glfwInit() == GLFW_FALSE) {
		LOG_ERROR("Failed to initialize GLFW");
		return false;
	}

	//Create a new GLFW window and make it current
	window = glfwCreateWindow(windowSize.x, windowSize.y, windowTitle.c_str(), nullptr, nullptr);
	glfwMakeContextCurrent(window);
	
	// Set our window resized callback
	glfwSetWindowSizeCallback(window, GlfwWindowResizedCallback);

	return true;
}

/// <summary>
/// Handles initializing GLAD and preparing our GLFW window for OpenGL calls
/// </summary>
/// <returns>True if GLAD is loaded, false if there was an error</returns>
bool initGLAD() {
	if (gladLoadGLLoader((GLADloadproc)glfwGetProcAddress) == 0) {
		LOG_ERROR("Failed to initialize Glad");
		return false;
	}
	return true;
}

/// <summary>
/// Draws a widget for saving or loading our scene
/// </summary>
/// <param name="scene">Reference to scene pointer</param>
/// <param name="path">Reference to path string storage</param>
/// <returns>True if a new scene has been loaded</returns>
bool DrawSaveLoadImGui(Scene::Sptr& scene, std::string& path) {
	// Since we can change the internal capacity of an std::string,
	// we can do cool things like this!
	ImGui::InputText("Path", path.data(), path.capacity());

	// Draw a save button, and save when pressed
	if (ImGui::Button("Save")) {
		scene->Save(path);
	}
	ImGui::SameLine();
	// Load scene from file button
	if (ImGui::Button("Load")) {
		// Since it's a reference to a ptr, this will
		// overwrite the existing scene!
		scene = nullptr;
		scene = Scene::Load(path);

		return true;
	}
	return false;
}

/// <summary>
/// Draws some ImGui controls for the given light
/// </summary>
/// <param name="title">The title for the light's header</param>
/// <param name="light">The light to modify</param>
/// <returns>True if the parameters have changed, false if otherwise</returns>
bool DrawLightImGui(const Scene::Sptr& scene, const char* title, int ix) {
	bool isEdited = false;
	bool result = false;
	Light& light = scene->Lights[ix];
	ImGui::PushID(&light); // We can also use pointers as numbers for unique IDs
	if (ImGui::CollapsingHeader(title)) {
		isEdited |= ImGui::DragFloat3("Pos", &light.Position.x, 0.01f);
		isEdited |= ImGui::ColorEdit3("Col", &light.Color.r);
		isEdited |= ImGui::DragFloat("Range", &light.Range, 0.1f);

		result = ImGui::Button("Delete");
	}
	if (isEdited) {
		scene->SetShaderLight(ix);
	}

	ImGui::PopID();
	return result;
}


void Food() 
{
	MeshResource::Sptr appleMesh = ResourceManager::CreateAsset<MeshResource>("cone.obj");
	MeshResource::Sptr bananaMesh = ResourceManager::CreateAsset<MeshResource>("cone.obj");

	Texture2D::Sptr    appleTex = ResourceManager::CreateAsset<Texture2D>("textures/apple.png");
	Texture2D::Sptr    bananaTex = ResourceManager::CreateAsset<Texture2D>("textures/banana.png");

	Material::Sptr appleMaterial = ResourceManager::CreateAsset<Material>();
	{
		appleMaterial->Name = "Apple";
		appleMaterial->MatShader = scene->BaseShader;
		appleMaterial->Texture = appleTex;
		appleMaterial->Shininess = 256.0f;

	}

	Material::Sptr bananaMaterial = ResourceManager::CreateAsset<Material>();
	{
		bananaMaterial->Name = "Banana";
		bananaMaterial->MatShader = scene->BaseShader;
		bananaMaterial->Texture = bananaTex;
		bananaMaterial->Shininess = 256.0f;

	}

	int random = 0;

	random = (rand() % 5) + 1;

	switch (random)
	{

	case(1):
	{
		GameObject::Sptr apple = scene->CreateGameObject("Apple");
		{
			// Set position in the scene
			apple->SetPostion(glm::vec3(5.0f, 2.0f, 1.0f));
			// Scale down the plane
			apple->SetScale(glm::vec3(0.5f));

			// Create and attach a render component
			RenderComponent::Sptr renderer = apple->Add<RenderComponent>();
			renderer->SetMesh(appleMesh);
			renderer->SetMaterial(appleMaterial);

			RigidBody::Sptr physics = apple->Add<RigidBody>(RigidBodyType::Static);
			physics->AddCollider(ConvexMeshCollider::Create());

			TriggerVolume::Sptr volume = apple->Add<TriggerVolume>();
			BoxCollider::Sptr collider = BoxCollider::Create(glm::vec3(5.0f, 2.0f, 1.0f));
			collider->SetPosition(glm::vec3(5.0f, 2.0f, 0.0f));
			volume->AddCollider(collider);

			foodinplay = true;
			worth = 1;
		}
		break;
	}

	case(2):
	{
		GameObject::Sptr apple = scene->CreateGameObject("Apple");
		{
			// Set position in the scene
			apple->SetPostion(glm::vec3(1.0f, 5.0f, 1.0f));
			// Scale down the plane
			apple->SetScale(glm::vec3(0.5f));

			// Create and attach a render component
			RenderComponent::Sptr renderer = apple->Add<RenderComponent>();
			renderer->SetMesh(appleMesh);
			renderer->SetMaterial(appleMaterial);

			RigidBody::Sptr physics = apple->Add<RigidBody>(RigidBodyType::Static);
			physics->AddCollider(ConvexMeshCollider::Create());

			TriggerVolume::Sptr volume = apple->Add<TriggerVolume>();
			BoxCollider::Sptr collider = BoxCollider::Create(glm::vec3(1.0f, 5.0f, 1.0f));
			collider->SetPosition(glm::vec3(1.0f, 5.0f, 1.0f));
			volume->AddCollider(collider);

			foodinplay = true;
			worth = 1;
		}
		break;
	}

	case(3):
	{
		GameObject::Sptr apple = scene->CreateGameObject("Apple");
		{
			// Set position in the scene
			apple->SetPostion(glm::vec3(-5.0f, 0.0f, 1.0f));
			// Scale down the plane
			apple->SetScale(glm::vec3(0.5f));

			// Create and attach a render component
			RenderComponent::Sptr renderer = apple->Add<RenderComponent>();
			renderer->SetMesh(appleMesh);
			renderer->SetMaterial(appleMaterial);

			RigidBody::Sptr physics = apple->Add<RigidBody>(RigidBodyType::Static);
			physics->AddCollider(ConvexMeshCollider::Create());
			TriggerVolume::Sptr volume = apple->Add<TriggerVolume>();
			BoxCollider::Sptr collider = BoxCollider::Create(glm::vec3(-5.0f, 0.0f, 1.0f));
			collider->SetPosition(glm::vec3(-5.0f, 0.0f, 1.0f));
			volume->AddCollider(collider);

			foodinplay = true;
			worth = 1;
		}
		break;
	}

	case(4):
	{
		GameObject::Sptr apple = scene->CreateGameObject("Apple");
		{
			// Set position in the scene
			apple->SetPostion(glm::vec3(0.0f, -5.0f, 1.0f));
			// Scale down the plane
			apple->SetScale(glm::vec3(0.5f));

			// Create and attach a render component
			RenderComponent::Sptr renderer = apple->Add<RenderComponent>();
			renderer->SetMesh(appleMesh);
			renderer->SetMaterial(appleMaterial);

			RigidBody::Sptr physics = apple->Add<RigidBody>(RigidBodyType::Static);
			physics->AddCollider(ConvexMeshCollider::Create());
			TriggerVolume::Sptr volume = apple->Add<TriggerVolume>();
			BoxCollider::Sptr collider = BoxCollider::Create(glm::vec3(0.0f, -5.0f, 1.0f));
			collider->SetPosition(glm::vec3(0.0f, -5.0f, 1.0f));
			volume->AddCollider(collider);

			foodinplay = true;
			worth = 1;
		}
		break;
	}

	case(5):
	{
		GameObject::Sptr banana = scene->CreateGameObject("Banana");
		{
			// Set position in the scene
			banana->SetPostion(glm::vec3(5.0f, 0.0f, 1.0f));
			// Scale down the plane
			banana->SetScale(glm::vec3(0.5f));

			// Create and attach a render component
			RenderComponent::Sptr renderer = banana->Add<RenderComponent>();
			renderer->SetMesh(bananaMesh);
			renderer->SetMaterial(bananaMaterial);

			RigidBody::Sptr physics = banana->Add<RigidBody>(RigidBodyType::Static);
			physics->AddCollider(ConvexMeshCollider::Create());
			TriggerVolume::Sptr volume = banana->Add<TriggerVolume>();
			BoxCollider::Sptr collider = BoxCollider::Create(glm::vec3(5.0f, 0.0f, 1.0f));
			collider->SetPosition(glm::vec3(5.0f, 0.0f, 1.0f));
			volume->AddCollider(collider);

			foodinplay = true;
			worth = 2;
		}
		break;
	}

	default:
	{
		GameObject::Sptr banana = scene->CreateGameObject("Banana");
		{
			// Set position in the scene
			banana->SetPostion(glm::vec3(7.0f, 7.0f, 1.0f));
			// Scale down the plane
			banana->SetScale(glm::vec3(0.5f));

			// Create and attach a render component
			RenderComponent::Sptr renderer = banana->Add<RenderComponent>();
			renderer->SetMesh(bananaMesh);
			renderer->SetMaterial(bananaMaterial);

			RigidBody::Sptr physics = banana->Add<RigidBody>(RigidBodyType::Static);
			physics->AddCollider(ConvexMeshCollider::Create());
			TriggerVolume::Sptr volume = banana->Add<TriggerVolume>();
			BoxCollider::Sptr collider = BoxCollider::Create(glm::vec3(7.0f, 7.0f, 1.0f));
			collider->SetPosition(glm::vec3(7.0f, 7.0f, 1.0f));
			volume->AddCollider(collider);

			foodinplay = true;
			worth = 2;
		}
		break;
	}

	}
}

void body()
{
	MeshResource::Sptr snakebodyMesh = ResourceManager::CreateAsset<MeshResource>("cone.obj");

	Texture2D::Sptr    bodyTex = ResourceManager::CreateAsset<Texture2D>("textures/snake_body.png");

	Material::Sptr snakebodyMaterial = ResourceManager::CreateAsset<Material>();
	{
		snakebodyMaterial->Name = "Snake";
		snakebodyMaterial->MatShader = scene->BaseShader;
		snakebodyMaterial->Texture = bodyTex;
		snakebodyMaterial->Shininess = 256.0f;

	}

	GameObject::Sptr snakebody = scene->CreateGameObject("Snake Body");
	{
		// Set position in the scene
		snakebody->SetPostion(glm::vec3(0.0f, 0.0f, 1.0f));
		// Scale down the plane
		snakebody->SetScale(glm::vec3(0.5f));

		// Create and attach a render component
		RenderComponent::Sptr renderer = snakebody->Add<RenderComponent>();
		renderer->SetMesh(snakebodyMesh);
		renderer->SetMaterial(snakebodyMaterial);

		snakebody->Add<JumpBehaviour>();

		RigidBody::Sptr physics = snakebody->Add<RigidBody>(RigidBodyType::Static);
		physics->AddCollider(ConvexMeshCollider::Create());

		TriggerVolume::Sptr volume = snakebody->Add<TriggerVolume>();
		BoxCollider::Sptr collider = BoxCollider::Create(glm::vec3(0.0f, 0.0f, 1.0f));
		collider->SetPosition(glm::vec3(0.0f, 0.0f, 1.0f));
		volume->AddCollider(collider);

	}

	return;
}

void eatfood()
{
	if (foodinplay == true)
	{
		
	}
}


int main() {
	Logger::Init(); // We'll borrow the logger from the toolkit, but we need to initialize it

	//Initialize GLFW
	if (!initGLFW())
		return 1;

	//Initialize GLAD
	if (!initGLAD())
		return 1;

	// Let OpenGL know that we want debug output, and route it to our handler function
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(GlDebugMessage, nullptr);

	// Initialize our ImGui helper
	ImGuiHelper::Init(window);

	// Initialize our resource manager
	ResourceManager::Init();

	// Register all our resource types so we can load them from manifest files
	ResourceManager::RegisterType<Texture2D>();
	ResourceManager::RegisterType<Material>();
	ResourceManager::RegisterType<MeshResource>();
	ResourceManager::RegisterType<Shader>();

	// Register all of our component types so we can load them from files
	ComponentManager::RegisterType<Camera>();
	ComponentManager::RegisterType<RenderComponent>();
	ComponentManager::RegisterType<RigidBody>();
	ComponentManager::RegisterType<TriggerVolume>();
	ComponentManager::RegisterType<RotatingBehaviour>();
	ComponentManager::RegisterType<JumpBehaviour>();
	ComponentManager::RegisterType<MaterialSwapBehaviour>();

	// GL states, we'll enable depth testing and backface fulling
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glClearColor(0.2f, 0.2f, 0.2f, 1.0f);

	bool loadScene = false;
	// For now we can use a toggle to generate our scene vs load from file
	if (loadScene) {
		ResourceManager::LoadManifest("manifest.json");
		scene = Scene::Load("scene.json");
	} 
	else {
		// Create our OpenGL resources
		Shader::Sptr uboShader = ResourceManager::CreateAsset<Shader>(std::unordered_map<ShaderPartType, std::string>{
			{ ShaderPartType::Vertex, "shaders/vertex_shader.glsl" }, 
			{ ShaderPartType::Fragment, "shaders/frag_blinn_phong_textured.glsl" }
		}); 
		
		Texture2D::Sptr    boxTexture = ResourceManager::CreateAsset<Texture2D>("textures/snake_background.png");
		Texture2D::Sptr    snakeTex = ResourceManager::CreateAsset<Texture2D>("textures/snake_head.png");
		MeshResource::Sptr snakeheadMesh = ResourceManager::CreateAsset<MeshResource>("cone.obj");

		// Create an empty scene
		scene = std::make_shared<Scene>();

		// I hate this
		scene->BaseShader = uboShader;

		// Create our materials
		Material::Sptr boxMaterial = ResourceManager::CreateAsset<Material>();
		{
			boxMaterial->Name = "Box";
			boxMaterial->MatShader = scene->BaseShader;
			boxMaterial->Texture = boxTexture;
			boxMaterial->Shininess = 2.0f;
		}	

		Material::Sptr snakeheadMaterial = ResourceManager::CreateAsset<Material>();
		{
			snakeheadMaterial->Name = "Snake";
			snakeheadMaterial->MatShader = scene->BaseShader;
			snakeheadMaterial->Texture = snakeTex;
			snakeheadMaterial->Shininess = 256.0f;

		}

		// Create some lights for our scene
		scene->Lights.resize(5);
		scene->Lights[0].Position = glm::vec3(0.0f, 0.0f, 5.0f);
		scene->Lights[0].Color = glm::vec3(1.0f, 1.0f, 0.0f);
		scene->Lights[0].Range = 20.0f;

		scene->Lights[1].Position = glm::vec3(10.0f, 10.0f, 5.0f);
		scene->Lights[1].Color = glm::vec3(1.0f, 1.0f, 0.0f);
		scene->Lights[1].Range = 20.0f;

		scene->Lights[2].Position = glm::vec3(-10.0f, 10.0f,5.0f);
		scene->Lights[2].Color = glm::vec3(1.0f, 1.0f, 0.0f);
		scene->Lights[2].Range = 20.0f;

		scene->Lights[3].Position = glm::vec3(10.0f, -10.0f, 5.0f);
		scene->Lights[3].Color = glm::vec3(1.0f, 1.0f, 0.0f);
		scene->Lights[3].Range = 20.0f;

		scene->Lights[4].Position = glm::vec3(-10.0f, -10.0f, 5.0f);
		scene->Lights[4].Color = glm::vec3(1.0f, 1.0f, 0.0f);
		scene->Lights[4].Range = 20.0f;

		// We'll create a mesh that is a simple plane that we can resize later
		MeshResource::Sptr planeMesh = ResourceManager::CreateAsset<MeshResource>();
		planeMesh->AddParam(MeshBuilderParam::CreatePlane(ZERO, UNIT_Z, UNIT_X, glm::vec2(1.0f)));
		planeMesh->GenerateMesh();

		// Set up the scene's camera
		GameObject::Sptr camera = scene->CreateGameObject("Main Camera");
		{
			camera->SetPostion(glm::vec3(0, 0, 10));
			camera->SetRotation(glm::vec3(0, 0, 0));
			//camera->LookAt(glm::vec3(0.0f));

			Camera::Sptr cam = camera->Add<Camera>();

			// Make sure that the camera is set as the scene's main camera!
			scene->MainCamera = cam;
		}

		// Set up all our sample objects
		GameObject::Sptr background = scene->CreateGameObject("Background");
		{
			// Scale up the plane
			background->SetScale(glm::vec3(20.0f, 20.0F, 1.0f));

			// Create and attach a RenderComponent to the object to draw our mesh
			RenderComponent::Sptr renderer = background->Add<RenderComponent>();
			renderer->SetMesh(planeMesh);
			renderer->SetMaterial(boxMaterial);

			// Attach a plane collider that extends infinitely along the X/Y axis
			RigidBody::Sptr physics = background->Add<RigidBody>(RigidBodyType::Static);
			physics->AddCollider(PlaneCollider::Create());

		}

		GameObject::Sptr snakehead = scene->CreateGameObject("Snake Head");
		{
			// Set position in the scene
			snakehead->SetPostion(glm::vec3(0.0f, 0.0f, 1.0f));
			// Scale down the plane
			snakehead->SetScale(glm::vec3(0.5f));

			RotatingBehaviour::Sptr behaviour = snakehead->Add<RotatingBehaviour>();
			behaviour->RotationSpeed = glm::vec3(0.0f, 0.0f, -90.0f);

			snakehead->Add<JumpBehaviour>();

			// Create and attach a render component
			RenderComponent::Sptr renderer = snakehead->Add<RenderComponent>();
			renderer->SetMesh(snakeheadMesh);
			renderer->SetMaterial(snakeheadMaterial);

			RigidBody::Sptr physics = snakehead->Add<RigidBody>(RigidBodyType::Dynamic);
			physics->AddCollider(ConvexMeshCollider::Create());

		}

		// Create a trigger volume for testing how we can detect collisions with objects!
		GameObject::Sptr toptrigger = scene->CreateGameObject("Top Trigger");
		{
			TriggerVolume::Sptr volume = toptrigger->Add<TriggerVolume>();
			BoxCollider::Sptr collider = BoxCollider::Create(glm::vec3(10.0f, 1.0f, 1.0f));
			collider->SetPosition(glm::vec3(0.0f, 10.0f, 0.5f));
			volume->AddCollider(collider);
		}

		GameObject::Sptr bottomtrigger = scene->CreateGameObject("Bottom Trigger");
		{
			TriggerVolume::Sptr volume1 = bottomtrigger->Add<TriggerVolume>();
			BoxCollider::Sptr collider1 = BoxCollider::Create(glm::vec3(10.0f, 1.0f, 1.0f));
			collider1->SetPosition(glm::vec3(0.0f, -10.0f, 0.5f));
			volume1->AddCollider(collider1);
		}

		/*GameObject::Sptr lefttrigger = scene->CreateGameObject("Left Trigger");
		{
			TriggerVolume::Sptr volume2 = lefttrigger->Add<TriggerVolume>();
			BoxCollider::Sptr collider2 = BoxCollider::Create(glm::vec3(0.0f, 10.0f, 1.0f));
			collider2->SetPosition(glm::vec3(-10.0f, 0.0f, 0.5f));
			volume2->AddCollider(collider2);
		}*/

		/*GameObject::Sptr righttrigger = scene->CreateGameObject("Right Trigger");
		{
			TriggerVolume::Sptr volume3 = bottomtrigger->Add<TriggerVolume>();
			BoxCollider::Sptr collider3 = BoxCollider::Create(glm::vec3(0.0f, 10.0f, 1.0f));
			collider3->SetPosition(glm::vec3(10.0f, 0.0f, 0.5f));
			volume3->AddCollider(collider3);
		}*/

		// Save the asset manifest for all the resources we just loaded
		ResourceManager::SaveManifest("manifest.json");
		// Save the scene to a JSON file
		scene->Save("scene.json");
	}

	// Call scene awake to start up all of our components
	scene->Window = window;
	scene->Awake();

	// We'll use this to allow editing the save/load path
	// via ImGui, note the reserve to allocate extra space
	// for input!
	std::string scenePath = "scene.json"; 
	scenePath.reserve(256); 

	bool isRotating = true;
	float rotateSpeed = 90.0f;

	// Our high-precision timer
	double lastFrame = glfwGetTime();


	BulletDebugMode physicsDebugMode = BulletDebugMode::None;
	float playbackSpeed = 1.0f;

	nlohmann::json editorSceneState;

	glm::mat4 rotate = glm::mat4(1.0f);
	glm::mat4 move = glm::mat4(1.0f);
	

	///// Game loop /////
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		ImGuiHelper::StartFrame();
		// Calculate the time since our last frame (dt)
		double thisFrame = glfwGetTime();
		float dt = static_cast<float>(thisFrame - lastFrame);


		if (foodinplay == false)
		{
			Food();
		}
		
		for (int i = 0; i < score; i++)
		{
			body();
		}

		eatfood();

		// Showcasing how to use the imGui library!
		bool isDebugWindowOpen = ImGui::Begin("Debugging");
		if (isDebugWindowOpen) {
			// Draws a button to control whether or not the game is currently playing
			static char buttonLabel[64];
			sprintf_s(buttonLabel, "%s###playmode", scene->IsPlaying ? "Exit Play Mode" : "Enter Play Mode");
			if (ImGui::Button(buttonLabel)) {
				// Save scene so it can be restored when exiting play mode
				if (!scene->IsPlaying) {
					editorSceneState = scene->ToJson();
				}

				// Toggle state
				scene->IsPlaying = !scene->IsPlaying;

				// If we've gone from playing to not playing, restore the state from before we started playing
				if (!scene->IsPlaying) {
					scene = nullptr;
					// We reload to scene from our cached state
					scene = Scene::FromJson(editorSceneState);
					// Don't forget to reset the scene's window and wake all the objects!
					scene->Window = window;
					scene->Awake();
				}
			}

			// Make a new area for the scene saving/loading
			ImGui::Separator();
			if (DrawSaveLoadImGui(scene, scenePath)) {
				// C++ strings keep internal lengths which can get annoying
				// when we edit it's underlying datastore, so recalcualte size
				scenePath.resize(strlen(scenePath.c_str()));

				// We have loaded a new scene, call awake to set
				// up all our components
				scene->Window = window;
				scene->Awake();
			}
			ImGui::Separator();
			// Draw a dropdown to select our physics debug draw mode
			if (BulletDebugDraw::DrawModeGui("Physics Debug Mode:", physicsDebugMode)) {
				scene->SetPhysicsDebugDrawMode(physicsDebugMode);
			}
			LABEL_LEFT(ImGui::SliderFloat, "Playback Speed:    ", &playbackSpeed, 0.0f, 10.0f);
			ImGui::Separator();
		}

		// Clear the color and depth buffers
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Update our application level uniforms every frame

		// Draw some ImGui stuff for the lights
		if (isDebugWindowOpen) {
			for (int ix = 0; ix < scene->Lights.size(); ix++) {
				char buff[256];
				sprintf_s(buff, "Light %d##%d", ix, ix);
				// DrawLightImGui will return true if the light was deleted
				if (DrawLightImGui(scene, buff, ix)) {
					// Remove light from scene, restore all lighting data
					scene->Lights.erase(scene->Lights.begin() + ix);
					scene->SetupShaderAndLights();
					// Move back one element so we don't skip anything!
					ix--;
				}
			}
			// As long as we don't have max lights, draw a button
			// to add another one
			if (scene->Lights.size() < scene->MAX_LIGHTS) {
				if (ImGui::Button("Add Light")) {
					scene->Lights.push_back(Light());
					scene->SetupShaderAndLights();
				}
			}
			// Split lights from the objects in ImGui
			ImGui::Separator();
		}

		dt *= playbackSpeed;

		// Perform updates for all components
		scene->Update(dt);

		// Grab shorthands to the camera and shader from the scene
		Camera::Sptr camera = scene->MainCamera;

		// Cache the camera's viewprojection
		glm::mat4 viewProj = camera->GetViewProjection();
		//DebugDrawer::Get().SetViewProjection(viewProj);

		// Update our worlds physics!
		scene->DoPhysics(dt);

		// Draw object GUIs
		if (isDebugWindowOpen) {
			scene->DrawAllGameObjectGUIs();

		}
		
		// The current material that is bound for rendering
		Material::Sptr currentMat = nullptr;
		Shader::Sptr shader = nullptr;

		// Render all our objects
		ComponentManager::Each<RenderComponent>([&](const RenderComponent::Sptr& renderable) {

			// If the material has changed, we need to bind the new shader and set up our material and frame data
			// Note: This is a good reason why we should be sorting the render components in ComponentManager
			if (renderable->GetMaterial() != currentMat) {
				currentMat = renderable->GetMaterial();
				shader = currentMat->MatShader;

				shader->Bind();
				shader->SetUniform("u_CamPos", scene->MainCamera->GetGameObject()->GetPosition());
				currentMat->Apply();
			}

			// Grab the game object so we can do some stuff with it
			GameObject* object = renderable->GetGameObject();

			// Set vertex shader parameters
			shader->SetUniformMatrix("u_ModelViewProjection", viewProj * object->GetTransform());
			shader->SetUniformMatrix("u_Model", object->GetTransform());
			shader->SetUniformMatrix("u_NormalMatrix", glm::mat3(glm::transpose(glm::inverse(object->GetTransform()))));

			// Draw the object
			renderable->GetMesh()->Draw();
		});


		// End our ImGui window
		ImGui::End();

		VertexArrayObject::Unbind();

		lastFrame = thisFrame;
		ImGuiHelper::EndFrame();
		glfwSwapBuffers(window);
	}

	// Clean up the ImGui library
	ImGuiHelper::Cleanup();

	// Clean up the resource manager
	ResourceManager::Cleanup();

	// Clean up the toolkit logger so we don't leak memory
	Logger::Uninitialize();
	return 0;
}