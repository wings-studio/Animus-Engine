#pragma once

#include "Aurora/Core/Object.hpp"
#include "Aurora/Graphics/RenderManager.hpp"
#include "Aurora/Graphics/Base/IRenderDevice.hpp"

namespace Aurora
{
	class Material;

	class AU_API PostProcessEffect : public ObjectBase
	{
	private:
		std::shared_ptr<Material> m_Material;
		bool m_Enabled;
	public:
		CLASS_OBJ(PostProcessEffect, ObjectBase);
	public:
		PostProcessEffect() : m_Material(nullptr), m_Enabled(true) {}
		~PostProcessEffect() override = default;

		virtual void Init() = 0;
		virtual void Render(const Texture_ptr& input, const Texture_ptr& output) = 0;
		[[nodiscard]] virtual bool CanRender() const { return Enabled(); }

		[[nodiscard]] const std::shared_ptr<Material>& GetMaterial() const { return m_Material; }
		void SetMaterial(std::shared_ptr<Material> material) { m_Material = std::move(material); }

		//TemporalRenderTarget& CopyCurrentRT(uint index) const;

		static DrawCallState PrepareState(const Shader_ptr& shader);
		static void RenderState(const DrawCallState& state);

		void SetEnabled(bool enabled) { m_Enabled = enabled; }
		bool& Enabled() { return m_Enabled; }
		[[nodiscard]] const bool& Enabled() const { return m_Enabled; }
	};
}