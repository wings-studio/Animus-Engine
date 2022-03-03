#include "ViewPortManager.hpp"
#include "Aurora/Engine.hpp"
#include "Base/IRenderDevice.hpp"

namespace Aurora
{

	void RenderViewPort::Resize(const Vector2i &size)
	{
		if(ViewPort.Width != size.x || ViewPort.Height != size.y)
		{
			int diffX = (int)glm::abs((int)size.x - (int)ViewPort.Width);
			int diffY = (int)glm::abs((int)size.y - (int)ViewPort.Height);

			AU_LOG_INFO("diff " , diffX, " " , diffY);

			// This prevents OpenGL from crashing when creating many textures in every tick
			if(diffX < 10 && diffY < 10)
			{
				return;
			}

			AU_LOG_INFO("Viewport resized");

			ViewPort.Width = size.x;
			ViewPort.Height = size.y;
			TargetDesc.Width = size.x;
			TargetDesc.Height = size.y;

			Target = GEngine->GetRenderDevice()->CreateTexture(TargetDesc);
		}
	}

	ViewPortManager::~ViewPortManager()
	{
		for(const auto& it : m_ViewPorts)
		{
			delete it.second;
		}
	}

	RenderViewPort *ViewPortManager::Create(uint8 id, GraphicsFormat format)
	{
		if(m_ViewPorts.contains(id))
		{
			return m_ViewPorts[id];
		}

		RenderViewPort* wp = (m_ViewPorts[id] = new RenderViewPort());
		wp->TargetDesc.Name = "ViewPort" + std::to_string(id);
		wp->TargetDesc.MipLevels = 1;
		wp->TargetDesc.IsRenderTarget = true;
		wp->TargetDesc.ImageFormat = format;
		return wp;
	}

	RenderViewPort *ViewPortManager::Get(uint8 id)
	{
		if(m_ViewPorts.contains(id))
		{
			return m_ViewPorts[id];
		}

		return nullptr;
	}

	bool ViewPortManager::Resize(uint8 id, const Vector2i &size)
	{
		if(m_ViewPorts.contains(id))
		{
			m_ViewPorts[id]->Resize(size);
			return true;
		}

		return false;
	}
}