#pragma once
#include <Lighthouse/Extended/Materials/Materials.hpp>
#include <Lighthouse/Extended/Rendering/IRenderExtension.hpp>
#include <Lighthouse/Extended/World/EntityRenderer.hpp>
#include <Lighthouse/Extended/World/Scene.hpp>
#include <Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>
#include <array>
#include <functional>

namespace Lighthouse
{
class Renderer final : public Lighthouse::IRenderTechnique
{
public:
  Renderer(Lighthouse::RenderGraphComposer* renderer, Scene* world,
                Lighthouse::RenderViewHandle window = {});
  ~Renderer() override;
  // Camera components and custom camera owners use the same path. Providers
  // execute during graph construction and must return stable component references.
  using CameraProvider = std::function<CameraView&(entt::entity)>;
  void SetCameraProvider(std::function<std::vector<entt::entity>()> entities, CameraProvider camera) {
    m_cameraEntities = std::move(entities); m_camera = std::move(camera);
  }
  void AddExtension(std::unique_ptr<IRenderExtension> extension) {
    if (!extension) throw std::invalid_argument("Null render extension");
    m_extensions.push_back(std::move(extension));
  }
  void BuildGraph(Lighthouse::TechniqueBuildContext& context) override;
  void SetAmbientRadiance(Vec3 radiance);
  void SetDeltaTime(float value) noexcept
  {
    m_deltaTime = value;
  }
  Lighthouse::RenderGraphResource GetOutput() const noexcept;

private:
  void BuildCamera(Lighthouse::TechniqueBuildContext&, entt::entity,
                   CameraView&);
  void EnsureLightVolumes();
  std::array<Lighthouse::DrawIndexedIndirectCommand, 3>
  VolumeCommands(const std::vector<LightData>& lights) const;
  void CreateOutputPipeline(Lighthouse::IRenderDevice& device);
  static bool Finite(Vec3 value);
  std::vector<LightData> GatherLights() const;
  std::vector<std::byte> LightingBytes(const std::vector<LightData>& lights,
                                       size_t capacity,
                                       const Mat4& viewProjection) const;
  static void SetViewport(Lighthouse::RenderPassDesc& pass, UVec2 size);
  Mat4 ViewProjection(entt::entity camera) const;
  void ReleaseDraws();
  struct LightingResources
  {
    std::shared_ptr<const ShadingModel> model;
    Lighthouse::DescriptorSetHandle set{}, lights{};
  };
  Lighthouse::RenderGraphComposer* m_renderer;
  Scene* m_world;
  Lighthouse::RenderViewHandle m_window;
  float m_deltaTime{};
  std::uint64_t m_buildId{};
  Lighthouse::RenderGraphResource m_output{Lighthouse::kInvalidGraphResource};
  std::vector<LightingResources> m_lighting;
  Lighthouse::SamplerHandle m_sampler{};
  std::vector<Lighthouse::BufferHandle> m_lightBuffers;
  std::array<Lighthouse::RenderMeshHandle, 3> m_volumes{};
  Lighthouse::DescriptorSetLayoutHandle m_outputSetLayout{};
  Lighthouse::PipelineLayoutHandle m_outputLayout{};
  Lighthouse::GraphicsPipelineHandle m_outputPipeline{};
  Lighthouse::ShaderModuleHandle m_outputVertex{}, m_outputFragment{};
  std::vector<Lighthouse::DescriptorSetHandle> m_outputSets;
  Vec3 m_ambientRadiance{0.03f};
  std::vector<std::unique_ptr<IRenderExtension>> m_extensions;
  std::function<std::vector<entt::entity>()> m_cameraEntities;
  CameraProvider m_camera;
  Materials m_materials;
  EntityRenderer m_entities;
};
} // namespace Lighthouse
