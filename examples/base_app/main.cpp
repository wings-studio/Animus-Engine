#include <Aurora/Aurora.hpp>
#include <Aurora/Resource/ResourceManager.hpp>

#include <Aurora/Graphics/Material/MaterialDefinition.hpp>
#include <Aurora/Graphics/Material/Material.hpp>
#include <Aurora/Graphics/ViewPortManager.hpp>
#include <Aurora/Resource/MaterialLoader.hpp>

#include <Shaders/World/PBRBasic/cb_pbr.h>

#include <Aurora/Memory/Aum.hpp>
#include <Aurora/Framework/Scene.hpp>
#include <Aurora/Framework/Actor.hpp>
#include <Aurora/Framework/SceneComponent.hpp>
#include <Aurora/Framework/CameraComponent.hpp>

#include <Aurora/Resource/AssimpModelLoader.hpp>

#include <imgui.h>
#include "Aurora/Editor/MainEditorPanel.hpp"

#include <Aurora/Graphics/OpenGL/GLRenderDevice.hpp>
#include <Aurora/Render/VgRender.hpp>

#include <stb_image_write.h>

using namespace Aurora;

void HSVtoRGB(float& fR, float& fG, float& fB, float& fH, float& fS, float& fV) {
	float fC = fV * fS; // Chroma
	float fHPrime = fmod(fH / 60.0, 6);
	float fX = fC * (1 - fabs(fmod(fHPrime, 2) - 1));
	float fM = fV - fC;

	if(0 <= fHPrime && fHPrime < 1) {
		fR = fC;
		fG = fX;
		fB = 0;
	} else if(1 <= fHPrime && fHPrime < 2) {
		fR = fX;
		fG = fC;
		fB = 0;
	} else if(2 <= fHPrime && fHPrime < 3) {
		fR = 0;
		fG = fC;
		fB = fX;
	} else if(3 <= fHPrime && fHPrime < 4) {
		fR = 0;
		fG = fX;
		fB = fC;
	} else if(4 <= fHPrime && fHPrime < 5) {
		fR = fX;
		fG = 0;
		fB = fC;
	} else if(5 <= fHPrime && fHPrime < 6) {
		fR = fC;
		fG = 0;
		fB = fX;
	} else {
		fR = 0;
		fG = 0;
		fB = 0;
	}

	fR += fM;
	fG += fM;
	fB += fM;
}

class TestActor : public Actor
{
private:
	int number;
	SceneComponent* m_Test;
public:
	CLASS_OBJ(TestActor, Actor);

	TestActor() : number(10)
	{
		std::cout << "TestActor created " << PointerToString(this) << std::endl;
	}

	void InitializeComponents() override
	{
		m_Test = AddComponent<SceneComponent>("Test");
		m_Test->GetTransform().Translation.x = 15;

		GetRootComponent()->GetTransform().Translation.x = 1000;
	}

	void Tick(double delta) override
	{
		std::cout << "tick" << std::endl;
	}

	void Test()
	{
		std::cout << "TestActor Test() " << number << " " << m_Test->GetTransform().Translation.x << " " << GetRootComponent()->GetTransform().Translation.x << std::endl;
	}

	~TestActor() override
	{
		std::cout << "TestActor deleted" << std::endl;
	}
};

class BaseAppContext : public AppContext
{
	MaterialDefinition_ptr matDef;
	std::shared_ptr<Material> mat;
	std::shared_ptr<Material> mat2;
	std::shared_ptr<Material> mat3;

	Mesh_ptr mesh = nullptr;

	MainEditorPanel* mainEditorPanel;

	~BaseAppContext()
	{
		delete mainEditorPanel;
	}

	void Init() override
	{
		SetGameContext<GameContext>();

		matDef = GEngine->GetResourceManager()->GetOrLoadMaterialDefinition("Assets/Materials/Base/Test2D.matd");

		mat = matDef->CreateInstance();

		//mat->SetVariable("Color"_HASH, Vector4(0, 1, 0, 1));

		mat2 = mat->Clone();
		//mat2->SetVariable("Color"_HASH, Vector4(1, 1, 1, 1));

		mat2->SetTexture("Texture"_HASH, GEngine->GetResourceManager()->LoadTexture("Assets/Textures/logo_as.png", GraphicsFormat::RGBA8_UNORM, {}));

		AssimpModelLoader modelLoader;

		MeshImportedData importedData = modelLoader.ImportModel("Test", GEngine->GetResourceManager()->LoadFile("Assets/transform_test.fbx"));

		if(importedData)
		{
			mesh = importedData.Mesh;
		}

		TestActor* actor = GetScene().SpawnActor<TestActor>("TestActor", Vector3(0, 0, 0));
		CameraComponent* cameraComponent = actor->AddComponent<CameraComponent>("Camera");

		for(SceneComponent* component : GetScene().GetComponents<SceneComponent>())
		{
			std::cout << "Component " << component->GetName() << std::endl;
		}

		GetScene().SpawnActor<TestActor>("TestActor2", Vector3(0, 0, 0));

		mainEditorPanel = new MainEditorPanel();
	}

	float a = 0;

	void Update(double delta) override
	{
		mainEditorPanel->Update();



		float fR = 0, fG = 0, fB = 0, fH = 0, fS = 0, fV = 0;

		fH = a * 255;
		fS = 0.19;
		fV = 255;

		HSVtoRGB(fR, fG, fB, fH, fS, fV);

		mat->SetVariable("Color"_HASH, Vector4(fR / 256.0f, fG / 256.0f, fB / 256.0f, 1));

		a += delta * 1.0f;
		if(a > 255) a = 0;
	}

	void Render() override
	{
		RenderViewPort* wp = GEngine->GetViewPortManager()->Get();

		DrawCallState drawCallState;
		drawCallState.PrimitiveType = EPrimitiveType::TriangleStrip;
		drawCallState.ViewPort = wp->ViewPort;
		drawCallState.BindTarget(0, wp->Target);

		drawCallState.ClearColor = Color::black();
		drawCallState.ClearColorTarget = true;

		GEngine->GetRenderDevice()->BindRenderTargets(drawCallState);
		GEngine->GetRenderDevice()->ClearRenderTargets(drawCallState);

		{
			drawCallState.ViewPort = FViewPort(0, 0, 256, 256);
			GEngine->GetRenderDevice()->SetViewPort(drawCallState.ViewPort);

			mat->BeginPass((uint8)EPassType::Ambient ,drawCallState);
			GEngine->GetRenderDevice()->Draw(drawCallState, {DrawArguments(4)}, false);
			mat->EndPass((uint8)EPassType::Ambient, drawCallState);
		}

		{
			drawCallState.ViewPort = FViewPort(0, 256 + 16, 256, 256);
			GEngine->GetRenderDevice()->SetViewPort(drawCallState.ViewPort);

			mat2->BeginPass((uint8)EPassType::Ambient ,drawCallState);
			GEngine->GetRenderDevice()->Draw(drawCallState, {DrawArguments(4)}, false);
			mat2->EndPass((uint8)EPassType::Ambient, drawCallState);
		}
	}

	void RenderVg() override
	{

	}
};

int main()
{
	WindowDefinition windowDefinition = {};
	windowDefinition.Width = 1270;
	windowDefinition.Height = 720;
	windowDefinition.HasOSWindowBorder = true;
	windowDefinition.Maximized = true;
	windowDefinition.Title = "BaseApp";

	Aurora::AuroraEngine engine;
	engine.Init(new BaseAppContext(), windowDefinition, true);
	engine.Run();
	return 0;
}