#include "WindowGameContext.hpp"

namespace Aurora
{
	WindowGameContext::WindowGameContext(Window_ptr window) : m_Window(std::move(window)), m_InputManager(m_Window->GetInputManager())
	{

	}

	const Window_ptr& WindowGameContext::GetWindow()
	{
		return m_Window;
	}

	const Input::IManager_ptr &WindowGameContext::GetInputManager()
	{
		return m_InputManager;
	}
}