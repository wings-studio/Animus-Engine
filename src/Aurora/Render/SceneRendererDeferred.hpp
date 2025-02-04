#pragma once

#include "SceneRenderer.hpp"

namespace Aurora
{
	class AU_API SceneRendererDeferred : public SceneRenderer
	{
	private:
		Buffer_ptr m_SkyLightBuffer;
		Buffer_ptr m_DirLightsBuffer;
		Buffer_ptr m_PointLightsBuffer;
		Buffer_ptr m_CompositeDefaultsBuffer;
		Shader_ptr m_CompositeShader;
		Shader_ptr m_HDRCompositeShader;
		Shader_ptr m_HDRCompositeShaderNoOutline;

		Shader_ptr m_OutlineShader;
		Buffer_ptr m_OutlineDescBuffer;
		Texture_ptr m_OutlineStripeTexture;
	public:
		SceneRendererDeferred();
		void LoadShaders() override;

		void Render(Scene* scene, CameraComponent* debugCamera) override;
	};
}
