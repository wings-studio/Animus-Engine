#pragma once

#include <vector>
#include "Aurora/Core/String.hpp"
#include "Aurora/Logger/Logger.hpp"

namespace Aurora
{
	class ConsoleWindow : public Logger::Sink
	{
	protected:
		struct SMessage
		{
			Logger::Severity Severity;
			String SeverityStr;
			String File;
			String Function;
			int Line;
			String Message;
		};
	private:
		bool m_AutoScroll;
		bool m_ScrollToBottom;
		std::vector<SMessage> m_Messages;
	public:
		ConsoleWindow() : m_AutoScroll(true), m_ScrollToBottom(false) {}

		void Draw();
		void Log(const Logger::Severity& severity, const std::string& severityStr, const std::string& file, const std::string& function, int line, const std::string& message) override;
	};
}
