#include "UserInterface.hpp"

#include "Aurora/Resource/ResourceManager.hpp"

namespace Aurora
{
	Rml::ElementDocument *UserInterface::LoadAndRegisterDocument(const String& path)
	{
		Rml::ElementDocument* doc = GEngine->GetRmlUI()->LoadDocument(path);

		if(!doc)
		{
			return nullptr;
		}

		return RegisterDocument(doc);
	}
}