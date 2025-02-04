#pragma once

#include "Aurora/Core/Types.hpp"
#include "Aurora/Core/Delegate.hpp"
#include "Aurora/Core/Library.hpp"
#include "Aurora/Tools/robin_hood.h"
#include "Base/Texture.hpp"
#include "ViewPort.hpp"

namespace Aurora
{
	class CameraComponent;

	struct RenderViewPort
	{
		FViewPort ViewPort;
		Vector2i ProxyLocation;
		TextureDesc TargetDesc;
		Texture_ptr Target;
		EventEmitter<const Vector2i&> ResizeEmitter;

		void Resize(const Vector2i &size, bool forceRecreation = false);

		[[nodiscard]] bool Changed(const Vector2i &size) const
		{
			return ViewPort.Width != size.x || ViewPort.Height != size.y;
		}

		[[nodiscard]] bool Initialized() const
		{
			return ViewPort.Width > 0 && ViewPort.Height > 0;
		}

		[[nodiscard]] int GetWidth() const { return ViewPort.Width; }
		[[nodiscard]] int GetHeight() const { return ViewPort.Height; }
	};

	class AU_API ViewPortManager
	{
	private:
		robin_hood::unordered_map<uint8, RenderViewPort*> m_ViewPorts;
	public:
		~ViewPortManager();

		RenderViewPort* Create(uint8 id, GraphicsFormat format);
		RenderViewPort* Get(uint8 id = 0);
		bool Resize(uint8 id, const Vector2i& size);
	};
}
