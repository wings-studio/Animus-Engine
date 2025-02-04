#pragma once

#include "Aurora/Core/Delegate.hpp"
#include "Aurora/Tools/ImGuiHelper.hpp"
#include "Aurora/Tools/ImGuizmo.h"

namespace Aurora
{
	class MainEditorPanel;
	class Scene;
	class Actor;
	class CameraComponent;
	struct RenderViewPort;

	class GameViewportWindow
	{
	private:
		MainEditorPanel* m_MainPanel;
		Scene* m_CurrentScene;
		RenderViewPort* m_RenderViewPort;
		UniqueEvent m_SceneChangeEvent;
		Actor* m_EditorCameraActor;
		CameraComponent* m_EditorCamera;

		bool m_MouseViewportGrabbed;

		ImGuizmo::OPERATION m_CurrentManipulatorOperation;
		ImGuizmo::MODE m_CurrentManipulatorMode;
		float m_FlySpeed;
	public:
		explicit GameViewportWindow(MainEditorPanel* m_MainPanel);
		~GameViewportWindow();

		void OnSceneChanged(Scene* scene);

		void Update(double delta);
		void HandleEditorCamera(double delta);

		CameraComponent* GetCurrentCamera();
		CameraComponent* GetEditorCamera() { return m_EditorCamera; }
	};
}