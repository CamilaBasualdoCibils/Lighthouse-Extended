#pragma once
#include <Lighthouse/Extended/Rendering/RenderFeatures.hpp>
#include <Lighthouse/Extended/Materials/Materials.hpp>
#include <Lighthouse/Extended/Resources/Material.hpp>
#include <Lighthouse/Extended/Resources/Model.hpp>
#include <array>

namespace Lighthouse
{
class SceneRenderStore;
struct SceneMeshGPU
{
  IRenderDevice* device{};
  BufferHandle vertices{}, indices{};
  VertexLayout layout;
  std::uint32_t indexCount{};
  ~SceneMeshGPU();
};
struct SceneImageGPU
{
  UVec2 resolution{};
  IRenderDevice* device{};
  ImageHandle image{};
  ImageViewHandle view{};
  ~SceneImageGPU();
};
struct SceneTextureGPU
{
  IRenderDevice* device{};
  ImageHandle image{};
  ImageViewHandle view{};
  SamplerHandle sampler{};
  std::shared_ptr<SceneImageGPU> sourceImage;
  ~SceneTextureGPU();
};
struct SceneMaterialGPU
{
  MaterialAsset data;
  bool normalMapped{};
  std::filesystem::path definition{"materials/GltfPBR.material.json"};
  MaterialLoadOptions options;
  std::map<std::string, std::shared_ptr<SceneTextureGPU>> resolvedTextures;
  std::array<std::shared_ptr<SceneTextureGPU>, 5> textures;
};
struct alignas(16) ForwardFrameData
{
  Mat4 viewProjection{1}, model{1};
  Vec4 cameraPosition{};
};
struct ForwardDrawBindings
{
  IRenderDevice* device{};
  BufferHandle frame{}, material{};
  DescriptorSetHandle frameSet{}, materialSet{}, lightSet{};
  std::shared_ptr<SceneMaterialGPU> source;
  std::shared_ptr<const MaterialInstance> instance;
  ~ForwardDrawBindings();
};

// Owned by the feature. Providers request compatible pipelines/bindings, then record draws.
class ForwardRenderServices
{
public:
  ForwardRenderServices(IRenderDevice&, std::string lightingShader);
  ~ForwardRenderServices();
  GraphicsPipelineHandle GetPipeline(const VertexLayout&, std::shared_ptr<SceneMaterialGPU>);
  std::shared_ptr<ForwardDrawBindings> CreateBindings(std::shared_ptr<SceneMaterialGPU>, BufferHandle lights);
  void BeginGeometry(ForwardRenderContext&);
  void Resolve(RenderFeatureContext&, RenderFeatureResult&);
private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

// CPU light extraction and shader contract for ordinary scene Light components.
class DefaultForwardLightingProvider final : public IForwardLightingProvider
{
public:
  explicit DefaultForwardLightingProvider(std::shared_ptr<SceneRenderStore> store,
                                          Vec3 ambientRadiance = Vec3(0.03f));
  void Update(RenderUpdateContext&) override;
  void BuildGraph(ForwardRenderContext&) override;
  std::vector<LightData> GetLights() const;
private:
  std::shared_ptr<SceneRenderStore> m_store;
  Vec3 m_ambient;
};
} // namespace Lighthouse
