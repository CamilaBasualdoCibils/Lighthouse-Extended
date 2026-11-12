#pragma once
#include <Lighthouse/Extended/Materials/Materials.hpp>

namespace Lighthouse
{
struct alignas(16) SceneViewData
{
  Mat4 viewProjection{1}, previousViewProjection{1};
  Vec4 cameraPosition{};
};
struct alignas(16) SceneObjectData
{
  Mat4 model{1}, previousModel{1};
  std::array<std::uint32_t, 4>
      material{}; // Parameter offset, texture-index offset.
};
static_assert(sizeof(SceneViewData) == 144 && sizeof(SceneObjectData) == 144);

// One binding group per scene view. Assets and storage are established at graph
// build, then reused; Execute only updates view/object bytes after graph
// retirement. The asset strategy is selected here, keeping fixed bindings as a
// fallback.
class SceneBindings
{
public:
  SceneBindings(IRenderDevice&,
                const std::vector<std::shared_ptr<const MaterialInstance>>&);
  ~SceneBindings();
  SceneBindings(const SceneBindings&) = delete;
  SceneBindings& operator=(const SceneBindings&) = delete;
  void Update(const SceneViewData&, std::span<const SceneObjectData>);
  DescriptorSetLayoutHandle FrameLayout() const
  {
    return m_frameLayout;
  }
  DescriptorSetLayoutHandle AssetLayout() const
  {
    return m_assetLayout;
  }
  DescriptorSetHandle FrameSet() const
  {
    return m_frameSet;
  }
  DescriptorSetHandle AssetSet() const
  {
    return m_assetSet;
  }
  SceneMaterialShaderOptions ShaderOptions() const
  {
    return {true, m_bindless, m_textureCount};
  }
  std::array<std::uint32_t, 4> MaterialOffsets(std::size_t draw) const
  {
    return m_offsets.at(draw);
  }
  std::array<BufferHandle, 4> Buffers() const
  {
    return {m_view, m_objects, m_parameters, m_textureIndices};
  }

private:
  void Release();
  IRenderDevice& m_device;
  bool m_bindless{};
  std::uint32_t m_textureCount{1};
  DescriptorSetLayoutHandle m_frameLayout{}, m_assetLayout{};
  DescriptorSetHandle m_frameSet{}, m_assetSet{};
  ImageHandle m_dummyImage{};
  ImageViewHandle m_dummyView{};
  SamplerHandle m_dummySampler{};
  BufferHandle m_view{}, m_objects{}, m_parameters{}, m_textureIndices{};
  std::vector<std::array<std::uint32_t, 4>> m_offsets;
  std::vector<std::shared_ptr<const MaterialInstance>> m_materials;
};
} // namespace Lighthouse
