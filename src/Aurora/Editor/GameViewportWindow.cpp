#include "GameViewportWindow.hpp"

#include "Utils.hpp"

#include "Aurora/Engine.hpp"
#include "Aurora/Aurora.hpp"
#include "Aurora/Core/Profiler.hpp"
#include "Aurora/Graphics/ViewPortManager.hpp"
#include "Aurora/Graphics/DShape.hpp"
#include "Aurora/Framework/Actor.hpp"
#include "Aurora/Framework/CameraComponent.hpp"
#include "Aurora/Framework/MeshComponent.hpp"
#include "Aurora/Framework/StaticMeshComponent.hpp"
#include "Aurora/Framework/Lights.hpp"
#include "Aurora/Framework/SkyLight.hpp"
#include "Aurora/Framework/Decal.hpp"
#include "Aurora/Resource/ResourceManager.hpp"
#include "Aurora/Physics/PhysicsWorld.hpp"

#include "MainEditorPanel.hpp"

namespace Aurora
{

	void SwitchToPlayMode()
	{
		// TODO: Copy scene here
		GameModeBase* gameModeBase = AppContext::GetGameModeBase();

		if(gameModeBase)
		{
			//gameModeBase->BeginPlay();
		}

		GEngine->GetInputManager()->LockCursor();
	}

	void SwitchToStopMode()
	{
		// TODO: Replace scene here

		GameModeBase* gameModeBase = AppContext::GetGameModeBase();

		if(gameModeBase)
		{
			//gameModeBase->BeginDestroy();
		}

		GEngine->GetInputManager()->UnlockCursor();
	}

	GameViewportWindow::GameViewportWindow(MainEditorPanel* mainEditorPanel)
		: m_MainPanel(mainEditorPanel),
		m_MouseViewportGrabbed(false),
		m_CurrentManipulatorOperation(ImGuizmo::OPERATION::TRANSLATE),
		m_CurrentManipulatorMode(ImGuizmo::MODE::WORLD),
		m_FlySpeed(10.0f), m_CurrentScene(nullptr), m_SceneChangeEvent()
	{
		m_RenderViewPort = GEngine->GetViewPortManager()->Create(0, GraphicsFormat::SRGBA8_UNORM);
		m_RenderViewPort->Resize({1270, 720});

		m_SceneChangeEvent = AppContext::GetSceneChangeEmitter().BindUnique(this, &GameViewportWindow::OnSceneChanged);
	}

	GameViewportWindow::~GameViewportWindow()
	{

	}

	void GameViewportWindow::OnSceneChanged(Scene* scene)
	{
		if(m_CurrentScene == scene)
			return;

		m_CurrentScene = scene;

		m_EditorCameraActor = scene->SpawnActor<Actor, CameraComponent>("EditorCameraActor", Vector3(-0.8f, 4.3f, 7.4f), Vector3(-31.0f, -4.0f, 0.0f));
		m_EditorCameraActor->SetActive(false);
		m_EditorCamera = CameraComponent::SafeCast(m_EditorCameraActor->GetRootComponent());
		m_EditorCamera->SetName("EditorCamera");
		m_EditorCamera->SetViewPort(m_RenderViewPort);
		m_EditorCamera->SetPerspective(70.0f, 0.1f, 2000.0f);
	}

	const char* GetIconForActor(Actor* actor)
	{
		if (actor->HasType(DirectionalLight::TypeID()))
			return ICON_FA_SUN;

		if (actor->HasType(PointLight::TypeID()))
			return ICON_FA_LIGHTBULB;

		if (actor->HasType(SpotLight::TypeID()))
			return ICON_FA_LIGHTBULB;

		if (actor->HasType(SkyLight::TypeID()))
			return ICON_FA_SUN;

		if (actor->GetRootComponent()->HasType(CameraComponent::TypeID()))
			return ICON_FA_CAMERA;

		return ICON_FA_USER;
	}

	bool m_ShowIcons = true;

	void GameViewportWindow::Update(double delta)
	{
		CPU_DEBUG_SCOPE("GameViewportWindow");

		if(m_MouseViewportGrabbed && !ImGui::GetIO().MouseDown[1])
		{
			m_MouseViewportGrabbed = false;
			GEngine->GetWindow()->SetCursorMode(ECursorMode::Normal);
		}

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_MenuBar);
		{
			if(ImGui::IsWindowMouseDown(1))
			{
				m_MouseViewportGrabbed = true;
				GEngine->GetWindow()->SetCursorMode(ECursorMode::Disabled);
			}

			if (ImGui::BeginMenuBar())
			{
				ImGui::Spacing();
				if (ImGui::BeginMenu("Play", !m_MainPanel->IsPlayMode()))
				{
					m_MainPanel->SetPlayMode(true);
					SwitchToPlayMode();
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Stop", m_MainPanel->IsPlayMode()))
				{
					m_MainPanel->SetPlayMode(false);
					SwitchToStopMode();
					ImGui::EndMenu();
				}

				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.25f));
				ImGui::Text("Fly speed: %.1fx", m_FlySpeed);
				ImGui::PopStyleColor();

				ImGui::Checkbox("Icons", &m_ShowIcons);

				ImGui::EndMenuBar();
			}

			ImVec2 avail_size = ImGui::GetContentRegionAvail();
			ImVec2 dragDelta = ImGui::GetMouseDragDelta();
			Vector2i viewPortSize = {avail_size.x, avail_size.y};

			if((dragDelta.x == 0 && dragDelta.y == 0) || !m_RenderViewPort->Initialized())
			{
				m_RenderViewPort->Resize(viewPortSize);
			}


			ImGuiViewport* imwp = ImGui::GetWindowViewport();
			// This outputs coords without the title bar... so we need to subtract that from CursorPos
			// because all positions in imgui are without the windows title bar
			// TODO: Create some helper function for this
			ImVec2 pos = ImGui::GetCurrentWindow()->DC.CursorPos;
			ImVec2 offset = imwp->Pos;
			// Sets proxy for rmlui cursor pos
			m_RenderViewPort->ProxyLocation = {pos.x - offset.x, pos.y - offset.y };

			EUI::Image(m_RenderViewPort->Target, (Vector2)m_RenderViewPort->ViewPort);

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RESOURCE_PATH"))
				{
					AU_LOG_INFO("Dropped into viewport: ", (char*)payload->Data);

					Path filePath = (char*)payload->Data;

					if(ResourceManager::IsFileType(filePath, FT_AMESH))
					{
						Mesh_ptr newMesh = GEngine->GetResourceManager()->LoadMesh(filePath);

						if (newMesh && newMesh->GetTypeID() == StaticMesh::TypeID())
						{
							Material_ptr material = GEngine->GetResourceManager()->GetOrLoadMaterialDefinition("Assets/Materials/Base/Textured.matd")->CreateInstance();

							auto* actor = AppContext::GetScene()->SpawnActor<Actor, StaticMeshComponent>(newMesh->Name, Vector3(0, 0, 0), {}, Vector3(1.0f));
							auto* meshComponent = StaticMeshComponent::Cast(actor->GetRootComponent());
							meshComponent->SetMesh(newMesh);

							for (auto &item : meshComponent->GetMaterialSet())
							{
								item.second.Material = material;
							}

							m_MainPanel->SetSelectedActor(actor);
						}
					}
				}

				ImGui::EndDragDropTarget();
			}


			if (m_EditorCameraActor->IsActive() && m_ShowIcons)
			{
				// Actor icons
				const float ICON_BASE_WIDTH = ImGui::CalcTextSize(ICON_FA_EYE).x;

				char tmps[10];
				Vector2 screenPos;

				for (Actor* actor : *AppContext::GetScene())
				{
					if (actor == m_EditorCameraActor)
						continue;

					const Vector3& location = actor->GetRootComponent()->GetTransform().GetLocation();
					if (!m_EditorCamera->GetScreenCoordinatesFromWorld(location, screenPos))
						continue;

					ImFormatString(tmps, sizeof(tmps), "%s", GetIconForActor(actor));
					ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + screenPos.x - ICON_BASE_WIDTH / 2.0f, pos.y + screenPos.y - ICON_BASE_WIDTH / 2.0f), IM_COL32_WHITE, tmps);
				}
			}

			//Manipulator
			ImGuizmo::SetDrawlist();
			ImGuizmo::SetRect(
				ImGui::GetWindowPos().x + offset.x,
				ImGui::GetWindowPos().y + offset.y,
				ImGui::GetWindowWidth(),
				ImGui::GetWindowHeight()
			);

			if (!m_MainPanel->IsPlayMode())
			{
				if(!ImGui::GetIO().WantTextInput && !m_MouseViewportGrabbed)
				{
					if(ImGui::GetIO().KeysDown[ImGui::GetKeyIndex(ImGuiKey_R)])
					{
						m_CurrentManipulatorOperation = ImGuizmo::OPERATION::SCALE;
					}

					if(ImGui::GetIO().KeysDown[ImGui::GetKeyIndex(ImGuiKey_E)])
					{
						m_CurrentManipulatorOperation = ImGuizmo::OPERATION::ROTATE;
					}

					if(ImGui::GetIO().KeysDown[ImGui::GetKeyIndex(ImGuiKey_W)])
					{
						m_CurrentManipulatorOperation = ImGuizmo::OPERATION::TRANSLATE;
					}
				}



				Matrix4 proj = m_EditorCamera->GetProjectionMatrix();
				Matrix4 view = m_EditorCamera->GetViewMatrix();

				Matrix4 transform;
				if(m_MainPanel->GetSelectedObjectTransform(transform))
				{
					bool manipulated = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), m_CurrentManipulatorOperation, m_CurrentManipulatorMode, glm::value_ptr(transform));
					if(manipulated)
					{
						(void)m_MainPanel->SetSelectedObjectTransform(transform);
					}
				}

				// Click to select object
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && not ImGuizmo::IsUsing() && not ImGuizmo::IsOver())
				{
					ImVec2 windowCursorPos = ImVec2(ImGui::GetMousePos().x - pos.x, ImGui::GetMousePos().y - pos.y);
					if (windowCursorPos.x >= 0 && windowCursorPos.y >= 0 && windowCursorPos.x < viewPortSize.x && windowCursorPos.y < viewPortSize.y)
					{
						Ray ray = m_EditorCamera->GetRayFromScreen(windowCursorPos.x, viewPortSize.y - windowCursorPos.y);

						//AU_LOG_INFO(glm::to_string(ray.Origin));

						std::vector<RayCastHitResult> results;
						if (GEngine->GetAppContext()->GetPhysicsWorld()->RayCast(ray.Origin, ray.Origin + ray.Direction * 1000.0f, results))
						{
							const RayCastHitResult& closesResult = results[0];
							m_MainPanel->SetSelectedActor(closesResult.HitActor);
						}
					}
				}

				//ImGuizmo::ViewManipulate(glm::value_ptr(view), 1, ImGui::GetWindowPos() + ImVec2(ImGui::GetWindowWidth() - 128, 30), ImVec2(128, 128), 0);
			}
		}
		ImGui::End();
		ImGui::PopStyleVar();

		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_MainPanel->SetPlayMode(false);
			SwitchToStopMode();
		}

		if (ImGui::IsKeyDown(ImGuiKey_LeftShift) && ImGui::IsKeyPressed(ImGuiKey_1))
		{
			GEngine->GetInputManager()->UnlockCursor();
		}

		if (m_EditorCameraActor->IsActive())
		{
			for (DecalComponent* decalComponent : AppContext::GetScene()->GetComponents<DecalComponent>())
			{
				DShapes::Box(decalComponent->GetTransformationMatrix(), AABB::FromExtent(Vector3(0.0f), Vector3(0.5f)), Color::white(), true, 1.0f, 0, false);
				DShapes::Arrow(decalComponent->GetLocation(), decalComponent->GetForwardVector(), Color::green(), true, 1.0f, 0, false);
			}
		}

		HandleEditorCamera(delta);
	}

	void GameViewportWindow::HandleEditorCamera(double delta)
	{
		bool playMode = m_MainPanel->IsPlayMode();

		m_EditorCameraActor->SetActive(!playMode);

		for (CameraComponent* camera : m_CurrentScene->GetComponents<CameraComponent>())
		{
			if(camera->GetOwner() == m_EditorCameraActor)
				continue;

			camera->SetActive(playMode);
		}

		if (m_MouseViewportGrabbed && !m_MainPanel->IsPlayMode())
		{
			m_FlySpeed += ImGui::GetIO().MouseWheel;

			if(m_FlySpeed < 0) m_FlySpeed = 0;

			if(auto* camera= m_EditorCameraActor->GetRootComponent())
			{
				float yaw = ImGui::GetIO().MouseDelta.y * -0.1f;
				float pitch = ImGui::GetIO().MouseDelta.x * -0.1f;
				camera->GetTransform().AddRotation(yaw, pitch, 0.0f);

				Matrix4 transform = camera->GetTransformationMatrix();

				if(ImGui::GetIO().KeysDown[ImGui::GetKeyIndex(ImGuiKey_W)])
				{
					camera->GetTransform().AddLocation(-Vector3(transform[2]) * (float)delta * m_FlySpeed);
				}

				if(ImGui::GetIO().KeysDown[ImGui::GetKeyIndex(ImGuiKey_S)])
				{
					camera->GetTransform().AddLocation(Vector3(transform[2]) * (float)delta * m_FlySpeed);
				}

				if(ImGui::GetIO().KeysDown[ImGui::GetKeyIndex(ImGuiKey_A)])
				{
					camera->GetTransform().AddLocation(-Vector3(transform[0]) * (float)delta * m_FlySpeed);
				}

				if(ImGui::GetIO().KeysDown[ImGui::GetKeyIndex(ImGuiKey_D)])
				{
					camera->GetTransform().AddLocation(Vector3(transform[0]) * (float)delta * m_FlySpeed);
				}
			}
		}
	}

	CameraComponent *GameViewportWindow::GetCurrentCamera()
	{
		if (!m_MainPanel->IsPlayMode())
		{
			return CameraComponent::Cast(m_EditorCameraActor->GetRootComponent());
		}

		for (CameraComponent* camera : m_CurrentScene->GetComponents<CameraComponent>())
		{
			if(camera->GetOwner() == m_EditorCameraActor)
				continue;

			return camera;
		}

		return nullptr;
	}
}