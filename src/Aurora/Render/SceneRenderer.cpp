#include "SceneRenderer.hpp"

#include "Aurora/Engine.hpp"
#include "Aurora/Resource/ResourceManager.hpp"

#include "Aurora/Core/Profiler.hpp"

#include "Aurora/Framework/Scene.hpp"
#include "Aurora/Framework/Entity.hpp"
#include "Aurora/Physics/Frustum.hpp"
#include "Aurora/Graphics/Material/Material.hpp"

#include "Shaders/vs_common.h"
#include "Shaders/ps_common.h"
#include "Shaders/PostProcess/cb_sky.h"
#include "Shaders/PostProcess/cb_ssao.h"

#include "PostProcessEffect.hpp"

#include <imgui.h>
#include <random>

namespace Aurora
{
	Texture_ptr ssaoNoiseTex = nullptr;

	SceneRenderer::SceneRenderer(Scene *scene, RenderManager* renderManager, IRenderDevice* renderDevice)
	: m_Scene(scene), m_RenderDevice(renderDevice), m_RenderManager(renderManager)
	{
		m_InstancingBuffer = m_RenderDevice->CreateBuffer(BufferDesc("InstanceBuffer", sizeof(Matrix4) * MAX_INSTANCES, EBufferType::UniformBuffer));

		m_PBRCompositeShader = GetEngine()->GetResourceManager()->LoadShader("PBR Composite", {
				{EShaderType::Vertex, "Assets/Shaders/fs_quad.vss"},
				{EShaderType::Pixel, "Assets/Shaders/PBR/pbr_composite.fss"},
		});

		m_SSAOShader = GetEngine()->GetResourceManager()->LoadShader("SSAO", {
			{EShaderType::Vertex, "Assets/Shaders/fs_quad.vss"},
			{EShaderType::Pixel, "Assets/Shaders/PostProcess/ssao.fss"},
		});

		m_SkyShader = GetEngine()->GetResourceManager()->LoadShader("Sky", {
				{EShaderType::Vertex, "Assets/Shaders/fs_quad.vss"},
				{EShaderType::Pixel, "Assets/Shaders/PostProcess/sky.fss"},
		});

		m_RenderSkyCubeShader = GetEngine()->GetResourceManager()->LoadComputeShader("Assets/Shaders/Sky/PreethamSky.glsl");

		std::uniform_real_distribution<GLfloat> randomFloats(0.0, 1.0); // generates random floats between 0.0 and 1.0
		std::default_random_engine generator;
		std::vector<glm::vec3> ssaoNoise;
		for (unsigned int i = 0; i < 16; i++)
		{
			ssaoNoise.push_back({randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f}); // rotate around z-axis (in tangent space)
		}

		TextureDesc desc;
		desc.Width = 4;
		desc.Height = 4;
		desc.ImageFormat = GraphicsFormat::RGB32_FLOAT;
		ssaoNoiseTex = m_RenderDevice->CreateTexture(desc);
		m_RenderDevice->WriteTexture(ssaoNoiseTex, 0, 0, ssaoNoise.data());
	}

	SceneRenderer::~SceneRenderer()
	{
		ssaoNoiseTex.reset();
	}

	Texture_ptr SceneRenderer::RenderPreethamSky(const Vector2ui& resolution, float turbidity, float azimuth, float inclination)
	{
		if(m_SkyCubeMap == nullptr)
		{
			m_SkyCubeMap = m_RenderManager->CreateRenderTarget("PreethamSky", {512, 512}, GraphicsFormat::RGBA32_FLOAT, EDimensionType::TYPE_CubeMap, 5, 6, TextureDesc::EUsage::Default, true);
		}

		static Vector4 lastData = Vector4(-1, -1, -1, -1);
		Vector4 data = Vector4(turbidity, azimuth, inclination, 1.0f);

		if(lastData == data) return m_SkyCubeMap;
		lastData = data;

		DispatchState dispatchState;
		dispatchState.Shader = m_RenderSkyCubeShader;
		dispatchState.BindTexture("o_CubeMap", m_SkyCubeMap, true);

		BEGIN_UB(Vector4, desc)
			*desc = data;
		END_CUB(Uniforms)

		m_RenderDevice->Dispatch(dispatchState, resolution.x / 32, resolution.y / 32, 6);
		m_RenderDevice->GenerateMipmaps(m_SkyCubeMap);

		return m_SkyCubeMap;
	}

	void SceneRenderer::AddVisibleEntity(Material* material, XMesh* mesh, uint meshSection, const Matrix4& transform)
	{
		CPU_DEBUG_SCOPE("SceneRenderer::AddVisibleEntity")
		entt::entity visibleEntityID = m_VisibleEntitiesRegistry.create();
		VisibleEntity& visibleEntity = m_VisibleEntitiesRegistry.emplace<VisibleEntity>(visibleEntityID);
		visibleEntity.Material = material;
		visibleEntity.Mesh = mesh;
		visibleEntity.MeshSection = meshSection;
		visibleEntity.Transform = transform;

		if(mesh->BeforeSectionAdd(mesh->m_Sections[meshSection], &m_CurrentCameraEntity))
		{
			m_VisibleEntities.emplace_back(visibleEntityID);
			m_VisibleTypeCounters[material->GetTypeID()]++;
		}
	}

	void SceneRenderer::PrepareRender(Frustum* frustum)
	{
		CPU_DEBUG_SCOPE("SceneRenderer::PrepareRender")
		m_VisibleEntities.clear();
		m_VisibleEntitiesRegistry.clear();
		m_VisibleTypeCounters.clear();

		for (int i = 0; i < SortTypeCount; ++i)
		{
			m_FinalSortedEntities[i].clear();
		}

		auto meshesView = m_Scene->GetRegistry().view<TransformComponent, MeshComponent>();

		for(entt::entity meshesEntt : meshesView)
		{
			auto [transform, meshComponent] = meshesView.get<TransformComponent, MeshComponent>(meshesEntt);
			const std::shared_ptr<XMesh>& mesh = meshComponent.Mesh;

			if(mesh == nullptr) continue;

			// Check frustum culling

			Matrix4 modelTransform = transform.GetTransform();

			/*
			 * material, mesh, transform
			 */

			if(mesh->m_HasBounds)
			{
				AABB aabb;

				if(mesh->m_BoundsPreTransformed)
				{
					aabb = mesh->m_Bounds;

				}
				else
				{
					aabb = mesh->m_Bounds;
					aabb *= modelTransform;
				}

				if(!frustum->IsBoxVisible(aabb))
				{
					continue;
				}
			}
			else
			{
				// TODO: Decide what to do with mesh with no bounds, rn we just enable rendering it
			}

			for (size_t i = 0; i < mesh->m_Sections.size(); ++i)
			{
				const XMesh::PrimitiveSection& section = mesh->m_Sections[i];

				matref material = meshComponent.GetMaterial(section.MaterialIndex);

				if(material == nullptr) continue;

				AddVisibleEntity(material.get(), mesh.get(), i, modelTransform);
			}
		}
	}

	void SceneRenderer::SortVisibleEntities()
	{
		CPU_DEBUG_SCOPE("SceneRenderer::SortVisibleEntities")
		// Sort by material base
		std::sort(m_VisibleEntities.begin(), m_VisibleEntities.end(), [this](const entt::entity current, const entt::entity other) -> bool {
			const auto& currentEntt = m_VisibleEntitiesRegistry.get<VisibleEntity>(current);
			const auto& otherEntt = m_VisibleEntitiesRegistry.get<VisibleEntity>(other);

			if(currentEntt.Mesh < otherEntt.Mesh) return true;
			if(currentEntt.Mesh > otherEntt.Mesh) return false;

			if(currentEntt.MeshSection < otherEntt.MeshSection) return true;
			if(currentEntt.MeshSection > otherEntt.MeshSection) return false;

			if(currentEntt.Material < otherEntt.Material) return true;
			if(currentEntt.Material > otherEntt.Material) return false;

			return false;
		});

		for (auto e : m_VisibleEntities)
		{
			const VisibleEntity& ve = m_VisibleEntitiesRegistry.get<VisibleEntity>(e);
			m_FinalSortedEntities[(uint8_t)ve.Material->GetSortType()].push_back(e);
		}
	}

	RenderSet SceneRenderer::BuildRenderSet()
	{
		std::vector<ModelContext> modelContexts;

		for (int i = 0; i < SortTypeCount; ++i)
		{
			XMesh* lastMesh = nullptr;
			Material* lastMaterial = nullptr;
			uint lastSection = 0;
			bool lastCanBeInstanced = true;

			ModelContext currentModelContext;

			for(auto e : m_FinalSortedEntities[i])
			{
				const VisibleEntity& visibleEntity = m_VisibleEntitiesRegistry.get<VisibleEntity>(e);
				XMesh::PrimitiveSection& section = visibleEntity.Mesh->m_Sections[visibleEntity.MeshSection];
				bool canBeInstanced = visibleEntity.Material->HasFlag(MF_INSTANCED);

				//std::cout << visibleEntity.Material->GetTypeName() << " - " << visibleEntity.Mesh << " - " << visibleEntity.MeshSection << std::endl;

				// Initialize last variables
				if(lastMesh == nullptr)
				{
					lastMesh = visibleEntity.Mesh;
					lastMaterial = visibleEntity.Material;
					lastCanBeInstanced = canBeInstanced;
					lastSection = visibleEntity.MeshSection;

					currentModelContext.Material = visibleEntity.Material;
					currentModelContext.Mesh = visibleEntity.Mesh;
					currentModelContext.MeshSection = &section;
					currentModelContext.Instances.push_back(visibleEntity.Transform);
					continue;
				}

				if(lastMesh == visibleEntity.Mesh && lastMaterial == visibleEntity.Material && lastCanBeInstanced == canBeInstanced && lastSection == visibleEntity.MeshSection)
				{
					currentModelContext.Instances.push_back(visibleEntity.Transform);
				}
				else
				{
					lastMesh = visibleEntity.Mesh;
					lastMaterial = visibleEntity.Material;
					lastCanBeInstanced = canBeInstanced;
					lastSection = visibleEntity.MeshSection;

					if(!currentModelContext.Instances.empty())
					{
						modelContexts.emplace_back(currentModelContext);
						currentModelContext = {};

						currentModelContext.Material = visibleEntity.Material;
						currentModelContext.Mesh = visibleEntity.Mesh;
						currentModelContext.MeshSection = &section;
						currentModelContext.Instances.push_back(visibleEntity.Transform);
					}
				}
			}

			if(!currentModelContext.Instances.empty())
			{
				modelContexts.emplace_back(currentModelContext);
			}

		}

		return modelContexts;
	}

	float lerp(float a, float b, float f)
	{
		return a + f * (b - a);
	}

	void SceneRenderer::Render(entt::entity cameraEntityID)
	{
		CPU_DEBUG_SCOPE("SceneRenderer::Render")
		assert(m_Scene);

		m_CurrentCameraEntity = Entity(cameraEntityID, m_Scene);
		auto& cameraTransform = m_CurrentCameraEntity.GetComponent<TransformComponent>();
		auto& camera = m_CurrentCameraEntity.GetComponent<CameraComponent>();
		Matrix4 projectionMatrix = camera.Projection;
		Matrix4 viewMatrix = glm::inverse(cameraTransform.GetTransform());
		Matrix4 projectionViewMatrix = projectionMatrix * viewMatrix;

		Frustum frustum(projectionViewMatrix);

		PrepareRender(&frustum);
		SortVisibleEntities();

		// Actual render

		auto albedoAndFlagsRT = m_RenderManager->CreateTemporalRenderTarget("Albedo", camera.Size, GraphicsFormat::RGBA8_UNORM);
		auto normalsRT = m_RenderManager->CreateTemporalRenderTarget("Normals", camera.Size, GraphicsFormat::RGBA8_UNORM);
		auto roughnessMetallicAORT = m_RenderManager->CreateTemporalRenderTarget("RoughnessMetallicAO", camera.Size, GraphicsFormat::RGBA8_UNORM);
		auto worldPosRT = m_RenderManager->CreateTemporalRenderTarget("WorldPosition", camera.Size, GraphicsFormat::RGBA32_FLOAT);

		auto depthRT = m_RenderManager->CreateTemporalRenderTarget("Depth", camera.Size, GraphicsFormat::D32);

		{
			DrawCallState drawState;
			//drawCallState.BindUniformBuffer("BaseVSData", m_BaseVSDataBuffer);
			drawState.BindUniformBuffer("Instances", m_InstancingBuffer);

			BEGIN_UB(BaseVSData, baseVsData)
				baseVsData->ProjectionMatrix = projectionMatrix;
				baseVsData->ViewMatrix = viewMatrix;
				baseVsData->ProjectionViewMatrix = projectionViewMatrix;
			END_UB(BaseVSData)

			drawState.ClearDepthTarget = true;
			drawState.ClearColorTarget = true;
			drawState.DepthStencilState.DepthEnable = true;
			drawState.RasterState.CullMode = ECullMode::Back;
			//drawState.ClearColor = Color(0,96,213);

			drawState.ViewPort = camera.Size;

			RenderSet globalRenderSet = BuildRenderSet();

			drawState.BindDepthTarget(depthRT, 0, 0);
			drawState.BindTarget(0, albedoAndFlagsRT);
			drawState.BindTarget(1, normalsRT);
			drawState.BindTarget(2, roughnessMetallicAORT);
			drawState.BindTarget(3, worldPosRT);

			m_RenderDevice->BindRenderTargets(drawState);
			m_RenderDevice->ClearRenderTargets(drawState);
			RenderPass(drawState, globalRenderSet, EPassType::Ambient);
			m_RenderManager->GetUniformBufferCache().Reset();
		}

		static Vector3 skyData = Vector3(2, 0, 0);
		ImGui::Begin("Sky");
		{
			ImGui::DragFloat("Turbidity", &skyData.x, 0.1f);
			ImGui::DragFloat("Azimuth", &skyData.y, 0.1f);
			ImGui::DragFloat("Inclination", &skyData.z, 0.1f);
		}
		ImGui::End();

		auto skyRT = m_RenderManager->CreateTemporalRenderTarget("Sky", camera.Size, GraphicsFormat::SRGBA8_UNORM);
		auto preetham = RenderPreethamSky({512, 512}, skyData.x, skyData.y, skyData.z);

		if(true)
		{ // Sky render
			GPU_DEBUG_SCOPE("Sky render");

			DrawCallState drawState;
			drawState.Shader = m_SkyShader;
			drawState.PrimitiveType = EPrimitiveType::TriangleStrip;
			drawState.ClearDepthTarget = false;
			drawState.ClearColorTarget = false;
			drawState.RasterState.CullMode = ECullMode::None;
			drawState.DepthStencilState.DepthEnable = false;
			drawState.ViewPort = camera.Size;

			drawState.BindTarget(0, skyRT);
			drawState.BindTexture("DepthTexture", depthRT);
			drawState.BindTexture("SkyCube", preetham);

			BEGIN_UB(SkyConstants, skyConstants)
				skyConstants->InvProjection = glm::inverse(camera.Projection);
				skyConstants->InvView = cameraTransform.GetTransform();
				skyConstants->CameraPos = Vector4(cameraTransform.Translation, 1);
				skyConstants->ViewPort = Vector4(camera.Size, 0, 0);
			END_UB(SkyConstants)

			m_RenderDevice->Draw(drawState, {DrawArguments(4)});
			m_RenderManager->GetUniformBufferCache().Reset();
		}

		/*static float ssaoRadius = 3.0f;
		static float ssaoBias = 0.025f;

		ImGui::Begin("SSAO");
		{
			ImGui::DragFloat("Bias", &ssaoBias, 0.01f);
			ImGui::DragFloat("Radius", &ssaoRadius, 0.01f);
		}
		ImGui::End();

		auto ssaoRT = m_RenderManager->CreateTemporalRenderTarget("SSAO", camera.Size, GraphicsFormat::SRGBA8_UNORM);
		if(false) { // SSAO
			DrawCallState drawState;
			drawState.Shader = m_SSAOShader;
			drawState.PrimitiveType = EPrimitiveType::TriangleStrip;
			drawState.ClearDepthTarget = false;
			drawState.ClearColorTarget = true;
			drawState.RasterState.CullMode = ECullMode::None;
			drawState.DepthStencilState.DepthEnable = false;
			drawState.ViewPort = camera.Size;

			drawState.BindTarget(0, ssaoRT);
			drawState.BindTexture("WorldPositionRT", worldPosRT);
			drawState.BindTexture("NormalWorldRT", normalsRT);
			drawState.BindTexture("NoiseTex", ssaoNoiseTex);

			std::uniform_real_distribution<GLfloat> randomFloats(0.0, 1.0); // generates random floats between 0.0 and 1.0
			std::default_random_engine generator;
			std::vector<glm::vec4> ssaoKernel;
			for (unsigned int i = 0; i < SSAO_SAMPLE_COUNT; ++i)
			{
				glm::vec4 sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, randomFloats(generator), 1);
				sample = glm::normalize(sample);
				sample *= randomFloats(generator);
				float scale = float(i) / float(SSAO_SAMPLE_COUNT);

				// scale samples s.t. they're more aligned to center of kernel
				scale = lerp(0.1f, 1.0f, scale * scale);
				sample *= scale;
				ssaoKernel.push_back(sample);
			}

			BEGIN_UB(SSAODesc, desc)
				desc->ProjectionMatrix = projectionMatrix;
				desc->ViewMatrix = viewMatrix;
				desc->NoiseData = vec4((Vector2)camera.Size / 4.0f, ssaoRadius, ssaoBias);
				memcpy(desc->Samples, ssaoKernel.data(), ssaoKernel.size() * sizeof(Vector4));
			END_UB(SSAODesc);

			m_RenderDevice->Draw(drawState, {DrawArguments(4)});
			m_RenderManager->GetUniformBufferCache().Reset();
		}*/

		auto compositedRT = m_RenderManager->CreateTemporalRenderTarget("CompositedRT", camera.Size, GraphicsFormat::RGBA8_UNORM);

		{ // Composite Deferred renderer and HRD
			GPU_DEBUG_SCOPE("PBR Composite");

			DrawCallState drawState;
			drawState.Shader = m_PBRCompositeShader;
			drawState.PrimitiveType = EPrimitiveType::TriangleStrip;
			drawState.ClearDepthTarget = false;
			drawState.ClearColorTarget = false;
			drawState.RasterState.CullMode = ECullMode::None;
			drawState.DepthStencilState.DepthEnable = false;

			drawState.ViewPort = camera.Size;

			drawState.BindTexture("AlbedoAndFlagsRT", albedoAndFlagsRT);
			drawState.BindTexture("NormalsRT", normalsRT);
			drawState.BindTexture("RoughnessMetallicAORT", roughnessMetallicAORT);
			drawState.BindTexture("SkyRT", skyRT);
			//drawState.BindTexture("SSAORT", ssaoRT);

			drawState.BindTarget(0, compositedRT);

			m_RenderDevice->Draw(drawState, {DrawArguments(4)});
			m_RenderManager->GetUniformBufferCache().Reset();
		}

		auto ppRT = m_RenderManager->CreateTemporalRenderTarget("PP Intermediate", compositedRT->GetDesc().GetSize(), compositedRT->GetDesc().ImageFormat);
		{ // PP's
			Texture_ptr currentInput = compositedRT;
			Texture_ptr currentOutput = ppRT;

			for(const auto& ppe : camera.PostProcessEffects)
			{
				if(!ppe->CanRender()) continue;
				GPU_DEBUG_SCOPE("PostProcess [" + String(ppe->GetTypeName()) + "]");
				ppe->Render(currentInput, currentOutput);
				m_RenderManager->Blit(currentOutput, currentInput);
			}

			{ // Blit to screen
				glEnable(GL_FRAMEBUFFER_SRGB);
				m_RenderManager->Blit(currentInput);
				glDisable(GL_FRAMEBUFFER_SRGB);
			}
		}
		ppRT.Free();
		compositedRT.Free();

		skyRT.Free();
		albedoAndFlagsRT.Free();
		normalsRT.Free();
		roughnessMetallicAORT.Free();
		worldPosRT.Free();
		depthRT.Free();
		//ssaoRT.Free();
	}

	void SceneRenderer::RenderPass(DrawCallState& drawCallState, const std::vector<ModelContext> &modelContexts, EPassType passType)
	{
		//CPU_DEBUG_SCOPE(String("RenderPass [") + PassTypesToString[(int)passType] + "]")
		GPU_DEBUG_SCOPE(String("RenderPass [") + PassTypesToString[(int)passType] + "]");

		std::vector<DrawArguments> drawArgs;

		Material* lastMaterial = nullptr;
		XMesh* lastMesh = nullptr;
		XMesh::PrimitiveSection* lastSection = nullptr;
		for (int i = 0; i < modelContexts.size(); ++i)
		{
			const ModelContext& mc = modelContexts[i];
			Material* mat = mc.Material;
			XMesh* mesh = mc.Mesh;
			const auto& section = *mc.MeshSection;

			if(lastMaterial != mat)
			{
				mat->BeginPass(drawCallState, passType);

				m_RenderDevice->SetShader(drawCallState.Shader);
				m_RenderDevice->BindShaderResources(drawCallState);

				m_RenderDevice->SetRasterState(drawCallState.RasterState);
				m_RenderDevice->SetDepthStencilState(drawCallState.DepthStencilState);

				lastMaterial = mat;
			}

			if(lastMesh != mesh || lastSection != mc.MeshSection)
			{
				drawCallState.PrimitiveType = section.PrimitiveType;
				drawCallState.InputLayoutHandle = section.Layout;
				drawCallState.SetIndexBuffer(mesh->m_Buffers[section.BufferIndex], section.IndexFormat);

				for (int a = 0; a < mesh->m_Buffers.size(); ++a)
				{
					drawCallState.SetVertexBuffer(a, mesh->m_Buffers[a]);
				}

				m_RenderDevice->BindShaderInputs(drawCallState);

				lastMesh = mesh;
				lastSection = mc.MeshSection;
			}

			if(mat->HasFlag(MF_INSTANCED) && !mc.Instances.empty())
			{
				//CPU_DEBUG_SCOPE("WriteInstances")
				/*auto* instancesPtr = m_RenderDevice->MapBuffer<ObjectInstanceData>(m_InstancingBuffer, EBufferAccess::WriteOnly);
				std::memcpy(instancesPtr, mc.Instances.data(), sizeof(ObjectInstanceData) * mc.Instances.size());
				m_RenderDevice->UnmapBuffer(m_InstancingBuffer);*/
				m_RenderDevice->WriteBuffer(m_InstancingBuffer, mc.Instances.data(), sizeof(Matrix4) * mc.Instances.size(), 0);
			}

			if(section.Ranges.size() == 1)
			{
				DrawArguments drawArguments;
				drawArguments.VertexCount = section.Ranges[0].IndexCount;
				drawArguments.StartIndexLocation = section.Ranges[0].IndexByteOffset;
				drawArguments.InstanceCount = mc.Instances.size();

				if(drawArguments.VertexCount == 0 || !section.Ranges[0].Enabled)
				{
					continue;
				}

				m_RenderDevice->DrawIndexed(drawCallState, {drawArguments});
			}
			else
			{
				drawArgs.clear();

				for(const XMesh::PrimitiveSection::Range& range : section.Ranges)
				{
					if(!range.Enabled) continue;

					DrawArguments drawArguments;
					drawArguments.VertexCount = range.IndexCount;
					drawArguments.StartIndexLocation = range.IndexByteOffset;
					drawArguments.InstanceCount = mc.Instances.size();

					if(drawArguments.VertexCount == 0)
					{
						continue;
					}

					drawArgs.emplace_back(drawArguments);
				}

				m_RenderDevice->DrawIndexed(drawCallState, drawArgs, false);
			}

			drawCallState.ClearDepthTarget = false;
			drawCallState.ClearColorTarget = false;

			if(i != modelContexts.size() - 1)
			{
				if(modelContexts[i + 1].Material != mat)
				{
					m_RenderManager->GetUniformBufferCache().Reset();
					mat->EndPass(drawCallState, passType);
					lastMaterial = nullptr;
				}
			}

		}

		if(lastMaterial)
		{
			lastMaterial->EndPass(drawCallState, passType);
		}
	}
}