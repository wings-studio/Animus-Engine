#include "SceneRenderer.hpp"

#include "Aurora/Engine.hpp"
#include "Aurora/Resource/ResourceManager.hpp"

#include "Aurora/Core/Profiler.hpp"

#include "Aurora/Framework/Scene.hpp"
#include "Aurora/Framework/Entity.hpp"
#include "Aurora/Physics/Frustum.hpp"
#include "Aurora/Graphics/Material/Material.hpp"
#include "Aurora/Graphics/DShape.hpp"

#include "Shaders/vs_common.h"
#include "Shaders/ps_common.h"
#include "Shaders/PostProcess/cb_sky.h"
#include "Shaders/PostProcess/cb_ssao.h"
#include "Shaders/PostProcess/cb_normal_bevel.h"
#include "Shaders/PostProcess/ub_bloom.h"
#include "Shaders/PBR/cb_pbr.h"

#include "PostProcessEffect.hpp"

#include <imgui.h>
#include <random>

namespace Aurora
{
	Texture_ptr ssaoNoiseTex = nullptr;

	const int PBRMipLevelCount = 5;

	SceneRenderer::SceneRenderer(Scene *scene, RenderManager* renderManager, IRenderDevice* renderDevice)
	: m_Scene(scene), m_RenderDevice(renderDevice), m_RenderManager(renderManager)
	{
		m_InstancingBuffer = m_RenderDevice->CreateBuffer(BufferDesc("InstanceBuffer", sizeof(Matrix4) * MAX_INSTANCES, EBufferType::UniformBuffer));

		m_PBRCompositeShader = GetEngine()->GetResourceManager()->LoadShader("PBR Composite", {
				{EShaderType::Vertex, "Assets/Shaders/fs_quad.vss"},
				{EShaderType::Pixel, "Assets/Shaders/PBR/PBRComposite.fss"},
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

		m_NormalBevelShader = GetEngine()->GetResourceManager()->LoadShader("Sky", {
			{EShaderType::Vertex, "Assets/Shaders/fs_quad.vss"},
			{EShaderType::Pixel, "Assets/Shaders/PostProcess/normal_bevel.fss"},
		});

		DShapes::Init();

		m_HDRShader = GetEngine()->GetResourceManager()->LoadShader("HDR", "Assets/Shaders/fs_quad.vss", "Assets/Shaders/PostProcess/hdr.fss");

		m_BloomShader = GetEngine()->GetResourceManager()->LoadComputeShader("Assets/Shaders/PostProcess/bloom.glsl");
		m_BloomDescBuffer = GetEngine()->GetRenderDevice()->CreateBuffer(BufferDesc("BloomDesc", sizeof(BloomDesc), EBufferType::UniformBuffer, EBufferUsage::DynamicDraw));

		m_PreFilterShader = GetEngine()->GetResourceManager()->LoadComputeShader("Assets/Shaders/PBR/PreFilter.glsl");
		m_IRRCShader = GetEngine()->GetResourceManager()->LoadComputeShader("Assets/Shaders/PBR/IrradianceConvolution.glsl");

		CreateBrdfMap();
	}

	SceneRenderer::~SceneRenderer()
	{
		ssaoNoiseTex.reset();
		DShapes::Destroy();
	}

	void SceneRenderer::CreateBrdfMap()
	{
		if(m_BrdfMap == nullptr)
		{
			m_BrdfMap = m_RenderManager->CreateRenderTarget("BrdfLut", {512, 512}, GraphicsFormat::RG16_FLOAT, EDimensionType::TYPE_2D, 1, 0, TextureDesc::EUsage::Default, true);
		}

		DispatchState dispatchState;
		dispatchState.Shader = GetEngine()->GetResourceManager()->LoadComputeShader("Assets/Shaders/PBR/BRDF.glsl");
		dispatchState.BindTexture("o_Image", m_BrdfMap, true);
		m_RenderDevice->Dispatch(dispatchState, 16, 16, 1);
	}

	Texture_ptr SceneRenderer::RenderPreethamSky(const Vector2ui& resolution, float turbidity, float azimuth, float inclination)
	{
		if(m_SkyCubeMap == nullptr)
		{
			m_SkyCubeMap = m_RenderManager->CreateRenderTarget("PreethamSky", resolution, GraphicsFormat::RGBA16_FLOAT, EDimensionType::TYPE_CubeMap, 5, 6, TextureDesc::EUsage::Default, true);
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

		{
			GPU_DEBUG_SCOPE("RenderPreethamSky");
			m_RenderDevice->Dispatch(dispatchState, resolution.x / 32, resolution.y / 32, 6);
			m_RenderDevice->GenerateMipmaps(m_SkyCubeMap);
		}

		RenderPrefilterEnvMap(m_SkyCubeMap);
		RenderIRRConvMap(m_SkyCubeMap);

		return m_SkyCubeMap;
	}

	Texture_ptr SceneRenderer::RenderPrefilterEnvMap(const Texture_ptr &envMap)
	{
		if(m_PreFilteredMap == nullptr)
		{
			m_PreFilteredMap = m_RenderManager->CreateRenderTarget("PrefilterEnvMap", envMap->GetDesc().GetSize(), GraphicsFormat::RGBA16_FLOAT, EDimensionType::TYPE_CubeMap, PBRMipLevelCount, 6, TextureDesc::EUsage::Default, true);
		}

		DispatchState dispatchState;
		dispatchState.Shader = m_PreFilterShader;
		dispatchState.BindTexture("_EnvironmentMap", envMap);

		uint32_t faceResolution = m_PreFilteredMap->GetDesc().Width;

		GPU_DEBUG_SCOPE("RenderPrefilterEnvMap");

		for (int mip = 0; mip < PBRMipLevelCount; ++mip)
		{
			GPU_DEBUG_SCOPE("Compute [mip=" + std::to_string(mip) + "]");

			float roughness = (float)mip / (float)(PBRMipLevelCount - 1);

			BEGIN_UB(Vector4, desc)
				*desc = Vector4(roughness, faceResolution, 0, 0);
			END_CUB(PreFilterDesc)

			auto[width, height] = m_PreFilteredMap->GetDesc().GetMipSize(mip);
			dispatchState.BindTexture("o_CubeMap", m_PreFilteredMap, true, TextureBinding::EAccess::Write, mip);
			m_RenderDevice->Dispatch(dispatchState, width / 32, height / 32, 6);
		}

		return m_PreFilteredMap;
	}

	Texture_ptr SceneRenderer::RenderIRRConvMap(const Texture_ptr &envMap)
	{
		GPU_DEBUG_SCOPE("RenderIRRConvMap");
		if(m_IRRCMap == nullptr)
		{
			m_IRRCMap = m_RenderManager->CreateRenderTarget("IRRCEnvMap", envMap->GetDesc().GetSize(), GraphicsFormat::RGBA16_FLOAT, EDimensionType::TYPE_CubeMap, 1, 6, TextureDesc::EUsage::Default, true);
		}

		Vector2ui size = m_IRRCMap->GetDesc().GetSize();

		DispatchState dispatchState;
		dispatchState.Shader = m_IRRCShader;
		dispatchState.BindSampler("_EnvironmentMap", Samplers::ClampClampNearestNearest);
		dispatchState.BindTexture("_EnvironmentMap", envMap);
		dispatchState.BindTexture("o_CubeMap", m_IRRCMap, true);
		m_RenderDevice->Dispatch(dispatchState, size.x / 16, size.y / 16, 6);

		return m_IRRCMap;
	}

	void SceneRenderer::AddVisibleEntity(Material* material, XMesh* mesh, uint meshSection, const Matrix4& transform)
	{
		CPU_DEBUG_SCOPE("SceneRenderer::AddVisibleEntity");
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
		CPU_DEBUG_SCOPE("SceneRenderer::PrepareRender");
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
		CPU_DEBUG_SCOPE("SceneRenderer::SortVisibleEntities");
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

	std::vector<float> CalcRations(const CameraComponent& camera, float lambda, uint8_t splits)
	{
		auto ratios = std::vector<float>(splits);
		float logArgument = (camera.ZFar / camera.ZNear);
		float uniArgument = (camera.ZFar - camera.ZNear);
		for (uint8_t i = 1; i <= splits; ++i) {
			float parameter = static_cast<float>(i) / static_cast<float>(splits);
			double result = (lambda * (camera.ZNear * std::pow(logArgument, parameter))) + ((1.0f - lambda)*(camera.ZNear + uniArgument * parameter));
			ratios[i-1] = static_cast<float>(result);
		}
		return ratios;
	}

	void SceneRenderer::Render(entt::entity cameraEntityID)
	{
		CPU_DEBUG_SCOPE("SceneRenderer::Render");
		assert(m_Scene);

		m_CurrentCameraEntity = Entity(cameraEntityID, m_Scene);
		auto& cameraTransform = m_CurrentCameraEntity.GetComponent<TransformComponent>();
		auto& camera = m_CurrentCameraEntity.GetComponent<CameraComponent>();
		Matrix4 projectionMatrix = camera.Projection;
		Matrix4 viewMatrix = glm::inverse(cameraTransform.GetTransform());
		Matrix4 projectionViewMatrix = projectionMatrix * viewMatrix;

		Frustum frustum(projectionViewMatrix);
		AABB frustumBounds = frustum.GetBounds();

		PrepareRender(&frustum);
		SortVisibleEntities();

		const DirectionalLightComponent* mainDirLight = nullptr;
		const TransformComponent* mainDirLightTransform = nullptr;
		{ // Prepare lights
			auto view = m_Scene->GetRegistry().view<TransformComponent, DirectionalLightComponent>();

			uint8_t splitCount = 4;
			std::vector<float> splits = CalcRations(camera, 1.0f, splitCount);

			for(entt::entity entity : view)
			{
				const TransformComponent& transformComponent = view.get<TransformComponent>(entity);
				const DirectionalLightComponent& directionalLightComponent = view.get<DirectionalLightComponent>(entity);

				Matrix4 lightViewMatrix = transformComponent.GetTransform();

				/*it will be a few mofor (uint8_t i = 0; i < splitCount; ++i)
				{
					std::cout << splits[i] << std::endl;
				}
				std::cout << "------------" << std::endl;*/

				mainDirLight = &directionalLightComponent;
				mainDirLightTransform = &transformComponent;
				break;
			}
		}

		// Actual render

		auto albedoAndFlagsRT = m_RenderManager->CreateTemporalRenderTarget("Albedo", camera.Size, GraphicsFormat::RGBA16_FLOAT);
		auto normalsRT = m_RenderManager->CreateTemporalRenderTarget("Normals", camera.Size, GraphicsFormat::RGBA8_UNORM);
		auto roughnessMetallicAORT = m_RenderManager->CreateTemporalRenderTarget("RoughnessMetallicAO", camera.Size, GraphicsFormat::RGBA8_UNORM);
		//auto worldPosRT = m_RenderManager->CreateTemporalRenderTarget("WorldPosition", camera.Size, GraphicsFormat::RGBA32_FLOAT);

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
			drawState.ClearColor = Color(255, 255, 255, 0);

			drawState.ViewPort = camera.Size;

			RenderSet globalRenderSet = BuildRenderSet();

			drawState.BindDepthTarget(depthRT, 0, 0);
			drawState.BindTarget(0, albedoAndFlagsRT);
			drawState.BindTarget(1, normalsRT);
			drawState.BindTarget(2, roughnessMetallicAORT);
			//drawState.BindTarget(3, worldPosRT);

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

		auto skyRT = m_RenderManager->CreateTemporalRenderTarget("Sky", camera.Size, GraphicsFormat::RGBA8_UNORM);

		float azimuth = 0;
		float inclination = 0;

		if(mainDirLightTransform)
		{
			azimuth = glm::radians(mainDirLightTransform->Rotation.x);
			inclination = glm::radians(mainDirLightTransform->Rotation.y);
		}

		auto preetham = RenderPreethamSky({128, 128}, skyData.x, azimuth, inclination);

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

		auto smoothNormalsRT = m_RenderManager->CreateTemporalRenderTarget("SmoothNormals", camera.Size, GraphicsFormat::RGBA8_UNORM);

		static bool NormalBevelEnabled = true;
		static NormalBevelEdgeData bevelEdgeData = {{25, 10, 0.03f, 30.0f}};
		{
			ImGui::Begin("EdgeBevel");
			{
				ImGui::Checkbox("Enabled##EdgeBevel",&NormalBevelEnabled);
				ImGui::DragFloat("EdgeOffset", &bevelEdgeData.EdgeData.x, 0.01f);
				ImGui::DragFloat("EdgeDistance", &bevelEdgeData.EdgeData.y, 0.01f);
				ImGui::DragFloat("BevelRadius", &bevelEdgeData.EdgeData.z, 0.01f);
				ImGui::DragFloat("BevelDistance", &bevelEdgeData.EdgeData.w, 0.01f);
			}
			ImGui::End();
		}

		if(NormalBevelEnabled)
		{ // NormalBevel
			GPU_DEBUG_SCOPE("NormalBevel");

			DrawCallState drawState = PostProcessEffect::PrepareState(m_NormalBevelShader);
			drawState.BindTarget(0, smoothNormalsRT);
			drawState.BindTexture("NormalMap", normalsRT);
			drawState.BindTexture("DepthMap", depthRT);
			drawState.ViewPort = camera.Size;

			BEGIN_UB(NormalBevelEdgeData, desc)
				*desc = bevelEdgeData;
			END_UB(NormalBevelEdgeData)

			PostProcessEffect::RenderState(drawState);
		}

		auto compositedRT = m_RenderManager->CreateTemporalRenderTarget("CompositedRT", camera.Size, GraphicsFormat::RGBA16_FLOAT);

		static Vector4 testOptions(1, 1, 1, 0);

		ImGui::Begin("Test PBR options");
		ImGui::DragFloat("roughness", &testOptions.x, 0.01f, 0, 1);
		ImGui::DragFloat("metallic", &testOptions.y, 0.01f, 0, 1);
		ImGui::DragFloat("ao", &testOptions.z, 0.01f, 0, 1);
		ImGui::End();

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
			drawState.BindTexture("NormalsRT", NormalBevelEnabled ? smoothNormalsRT : normalsRT);
			drawState.BindTexture("RoughnessMetallicAORT", roughnessMetallicAORT);
			//drawState.BindTexture("WorldPosRT", worldPosRT);
			drawState.BindTexture("SkyRT", skyRT);
			//drawState.BindTexture("SSAORT", ssaoRT);

			drawState.BindTexture("DepthMap", depthRT);

			drawState.BindTexture("PreFilteredMap", m_PreFilteredMap);
			drawState.BindTexture("IrradianceConvolutionMap", m_IRRCMap);
			drawState.BindTexture("BrdfLutMap", m_BrdfMap);

			drawState.BindSampler("PreFilteredMap", Samplers::ClampClampLinearLinear);
			drawState.BindSampler("IrradianceConvolutionMap", Samplers::ClampClampLinearLinear);
			drawState.BindSampler("BrdfLutMap", Samplers::ClampClampLinearLinear);

			BEGIN_UB(PBRDesc, desc)
				desc->u_InvProjectionView = glm::inverse(projectionViewMatrix);
				desc->CameraPos = Vector4(cameraTransform.Translation, 0.0);
				desc->TestOptions = testOptions;
			END_UB(PBRDesc)

			if(mainDirLight && mainDirLightTransform)
			{
				BEGIN_UB(DirectionalLight, desc)
					desc->Direction = mainDirLightTransform->Forward;
					desc->Radiance = mainDirLight->LightColor;
					desc->Multiplier = mainDirLight->Intensity;
				END_UB(DirectionalLight)
			}

			drawState.BindTarget(0, compositedRT);

			m_RenderDevice->Draw(drawState, {DrawArguments(4)});
			m_RenderManager->GetUniformBufferCache().Reset();
		}

		{ // Debug shapes
			GPU_DEBUG_SCOPE("Debug Shapes");
			DrawCallState drawState;

			BEGIN_UB(BaseVSData, baseVsData)
				baseVsData->ProjectionMatrix = projectionMatrix;
				baseVsData->ViewMatrix = viewMatrix;
				baseVsData->ProjectionViewMatrix = projectionViewMatrix;
			END_UB(BaseVSData)

			drawState.ClearDepthTarget = false;
			drawState.ClearColorTarget = false;
			drawState.DepthStencilState.DepthEnable = true;
			drawState.RasterState.CullMode = ECullMode::Back;

			drawState.ViewPort = camera.Size;

			drawState.BindDepthTarget(depthRT, 0, 0);
			drawState.BindTarget(0, compositedRT);

			m_RenderDevice->BindRenderTargets(drawState);
			// Render debug shapes
			DShapes::Render(drawState);

			m_RenderManager->GetUniformBufferCache().Reset();
		}

		albedoAndFlagsRT.Free();
		roughnessMetallicAORT.Free();
		smoothNormalsRT.Free();

		TemporalRenderTarget bloomRTs[3];
		if(m_BloomSettings.Enabled)
		{ // Bloom
			GPU_DEBUG_SCOPE("Bloom");

			Vector2ui bloomTexSize = camera.Size / 2u;
			bloomTexSize += Vector2ui(m_BloomComputeWorkgroupSize - (bloomTexSize.x % m_BloomComputeWorkgroupSize), m_BloomComputeWorkgroupSize - (bloomTexSize.y % m_BloomComputeWorkgroupSize));

			TextureDesc mipDesc;
			mipDesc.Width = bloomTexSize.x;
			mipDesc.Height = bloomTexSize.y;

			uint32_t mips = mipDesc.GetMipLevelCount() - 2 - 2;
			//mips -= mips / 3;

			for (int i = 0; i < 3; ++i)
			{
				std::string texName = "BloomCompute #" + std::to_string(i);

				bloomRTs[i] = m_RenderManager->CreateTemporalRenderTarget(texName, bloomTexSize, GraphicsFormat::RGBA16_FLOAT, EDimensionType::TYPE_2D, mips, 0, TextureDesc::EUsage::Default, true);
			}

			uint32_t workGroupsX = bloomTexSize.x / m_BloomComputeWorkgroupSize;
			uint32_t workGroupsY = bloomTexSize.y / m_BloomComputeWorkgroupSize;

			BloomDesc bloomDesc;
			bloomDesc.Params = { m_BloomSettings.Threshold, m_BloomSettings.Threshold - m_BloomSettings.Knee, m_BloomSettings.Knee * 2.0f, 0.25f / m_BloomSettings.Knee };
			bloomDesc.LodAndMode.x = 0;
			bloomDesc.LodAndMode.y = BLOOM_MODE_PREFILTER;

			DispatchState dispatchState;
			dispatchState.Shader = m_BloomShader;
			dispatchState.BindUniformBuffer("BloomDesc", m_BloomDescBuffer);
			dispatchState.BindSampler("u_Texture", Samplers::ClampClampLinearLinear);
			dispatchState.BindSampler("u_BloomTexture", Samplers::ClampClampLinearLinear);

			GetEngine()->GetRenderDevice()->WriteBuffer(m_BloomDescBuffer, &bloomDesc);

			dispatchState.BindTexture("u_Texture", compositedRT);
			dispatchState.BindTexture("u_BloomTexture", compositedRT);
			dispatchState.BindTexture("o_Image", bloomRTs[0], true);

			// Prefilter
			GetEngine()->GetRenderDevice()->Dispatch(dispatchState, workGroupsX, workGroupsY, 1);

			// Downsample
			bloomDesc.LodAndMode.y = BLOOM_MODE_DOWNSAMPLE;

			for (uint32_t i = 1; i < mips; i++)
			{
				auto[mipWidth, mipHeight] = bloomRTs[0]->GetDesc().GetMipSize(i);

				workGroupsX = (uint32_t)glm::ceil((float)mipWidth / (float)m_BloomComputeWorkgroupSize);
				workGroupsY = (uint32_t)glm::ceil((float)mipHeight / (float)m_BloomComputeWorkgroupSize);

				{ // Ping
					//Output
					dispatchState.BindTexture("o_Image", bloomRTs[1], true, Aurora::TextureBinding::EAccess::Write, i);
					// Input
					bloomDesc.LodAndMode.x = (float)i - 1.0f;
					GetEngine()->GetRenderDevice()->WriteBuffer(m_BloomDescBuffer, &bloomDesc);

					dispatchState.BindTexture("u_Texture", bloomRTs[0]);
					GetEngine()->GetRenderDevice()->Dispatch(dispatchState, workGroupsX, workGroupsY, 1);
				}

				{ // Pong
					//Output
					dispatchState.BindTexture("o_Image", bloomRTs[0], true, Aurora::TextureBinding::EAccess::Write, i);

					// Input
					bloomDesc.LodAndMode.x = (float)i;
					GetEngine()->GetRenderDevice()->WriteBuffer(m_BloomDescBuffer, &bloomDesc);
					dispatchState.BindTexture("u_Texture", bloomRTs[1]);
					GetEngine()->GetRenderDevice()->Dispatch(dispatchState, workGroupsX, workGroupsY, 1);
				}
			}

			// Upsample first
			bloomDesc.LodAndMode.y = BLOOM_MODE_UPSAMPLE_FIRST;

			{ // Upsample First
				//Output
				dispatchState.BindTexture("o_Image", bloomRTs[2], true, Aurora::TextureBinding::EAccess::Write, mips - 1);

				// Input
				bloomDesc.LodAndMode.x--;
				GetEngine()->GetRenderDevice()->WriteBuffer(m_BloomDescBuffer, &bloomDesc);
				dispatchState.BindTexture("u_Texture", bloomRTs[0]);

				auto [mipWidth, mipHeight] = bloomRTs[2]->GetDesc().GetMipSize(mips - 1);
				workGroupsX = (uint32_t)glm::ceil((float)mipWidth / (float)m_BloomComputeWorkgroupSize);
				workGroupsY = (uint32_t)glm::ceil((float)mipHeight / (float)m_BloomComputeWorkgroupSize);

				GetEngine()->GetRenderDevice()->Dispatch(dispatchState, workGroupsX, workGroupsY, 1);
			}

			// Upsample
			bloomDesc.LodAndMode.y = BLOOM_MODE_UPSAMPLE;

			for (int32_t mip = mips - 2; mip >= 0; mip--)
			{
				auto [mipWidth, mipHeight] = bloomRTs[2]->GetDesc().GetMipSize(mip);
				workGroupsX = (uint32_t)glm::ceil((float)mipWidth / (float)m_BloomComputeWorkgroupSize);
				workGroupsY = (uint32_t)glm::ceil((float)mipHeight / (float)m_BloomComputeWorkgroupSize);

				//Output
				dispatchState.BindTexture("o_Image", bloomRTs[2], true, Aurora::TextureBinding::EAccess::Write, mip);

				// Input
				bloomDesc.LodAndMode.x = mip;
				GetEngine()->GetRenderDevice()->WriteBuffer(m_BloomDescBuffer, &bloomDesc);
				dispatchState.BindTexture("u_Texture", bloomRTs[0]);
				dispatchState.BindTexture("u_BloomTexture", bloomRTs[2]);

				GetEngine()->GetRenderDevice()->Dispatch(dispatchState, workGroupsX, workGroupsY, 1);
			}
		}

		auto finalSceneRT = m_RenderManager->CreateTemporalRenderTarget("FinalSceneRT", camera.Size, GraphicsFormat::RGBA8_UNORM);
		{ // HDR and GammaCorrection
			GPU_DEBUG_SCOPE("HDR");
			DrawCallState drawState = PostProcessEffect::PrepareState(m_HDRShader);
			drawState.ViewPort = camera.Size;
			drawState.BindTarget(0, finalSceneRT);
			drawState.BindTexture("SceneHRDTexture", compositedRT);
			drawState.BindSampler("SceneHRDTexture", Samplers::ClampClampNearestNearest);

			if(m_BloomSettings.Enabled) drawState.BindTexture("BloomTexture", bloomRTs[2]);
			drawState.BindSampler("BloomTexture", Samplers::ClampClampLinearLinear);
			PostProcessEffect::RenderState(drawState);
		}

		if(m_BloomSettings.Enabled) for (int i = 0; i < 3; ++i) bloomRTs[i].Free();

		auto ppRT = m_RenderManager->CreateTemporalRenderTarget("PP Intermediate", camera.Size, finalSceneRT->GetDesc().ImageFormat);
		{ // PP's
			Texture_ptr currentInput = finalSceneRT;
			Texture_ptr currentOutput = ppRT;

			for(const auto& ppe : camera.PostProcessEffects)
			{
				if(!ppe->CanRender()) continue;
				GPU_DEBUG_SCOPE("PostProcess [" + String(ppe->GetTypeName()) + "]");
				ppe->Render(currentInput, currentOutput);
				m_RenderManager->Blit(currentOutput, currentInput);
			}

			{ // Blit to screen
				//glEnable(GL_FRAMEBUFFER_SRGB);
				m_RenderManager->Blit(currentInput);
				//glDisable(GL_FRAMEBUFFER_SRGB);
			}
		}
		finalSceneRT.Free();
		skyRT.Free();
		normalsRT.Free();
		ppRT.Free();
		compositedRT.Free();
		//worldPosRT.Free();
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