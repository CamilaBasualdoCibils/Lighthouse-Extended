#include <Lighthouse/Extended/Rendering/SceneBindings.hpp>
#include <cstring>
namespace Lighthouse
{
SceneBindings::SceneBindings(
    IRenderDevice& device,
    const std::vector<std::shared_ptr<const MaterialInstance>>& materials)
    : m_device(device), m_bindless(device.GetCapabilities().bindlessResources),
      m_materials(materials)
{
  try
  {
    std::vector<std::array<uint32_t, 4>> parameters;
    std::vector<uint32_t> indices;
    std::vector<DescriptorImageInfo> textures;
    std::map<std::shared_ptr<const MaterialInstance>, std::array<uint32_t, 4>>
        offsets;
    for (const auto& material : materials)
    {
      auto it = offsets.find(material);
      if (it == offsets.end())
      {
        std::array<uint32_t, 4> record{uint32_t(parameters.size()),
                                       uint32_t(indices.size()), 0, 0};
        parameters.insert(parameters.end(), material->parameterData.begin(),
                          material->parameterData.end());
        for (const auto& texture : material->textures)
        {
          auto textureIt =
              std::find_if(textures.begin(), textures.end(),
                           [&](auto value)
                           {
                             return value.image == texture.image &&
                                    value.sampler == texture.sampler;
                           });
          const auto index = uint32_t(textureIt - textures.begin());
          if (textureIt == textures.end())
            textures.push_back(texture);
          indices.push_back(index);
        }
        it = offsets.emplace(material, record).first;
      }
      m_offsets.push_back(it->second);
    }
    if (parameters.empty())
      parameters.resize(1);
    if (indices.empty())
      indices.resize(1);
    const auto host = MemoryAccess::CPUWrite | MemoryAccess::GPURead;
    m_view = device.CreateBuffer({.size = sizeof(SceneViewData),
                                  .usage = BufferUsage::Uniform,
                                  .memory = {.requiredAccess = host}});
    m_objects = device.CreateBuffer(
        {.size = std::max(std::size_t(1), materials.size()) *
                 sizeof(SceneObjectData),
         .usage = BufferUsage::Storage,
         .memory = {.requiredAccess = host}});
    m_parameters = device.CreateBuffer(
        {.size = parameters.size() * sizeof(parameters[0]),
         .usage = BufferUsage::Storage,
         .initialData = std::as_bytes(std::span(parameters))});
    m_textureIndices =
        device.CreateBuffer({.size = indices.size() * sizeof(indices[0]),
                             .usage = BufferUsage::Storage,
                             .initialData = std::as_bytes(std::span(indices))});
    constexpr auto stages =
        ShaderStageFlags::Vertex | ShaderStageFlags::Fragment;
    m_frameLayout = device.CreateDescriptorSetLayout(
        {.bindings = {{.binding = 0,
                       .type = DescriptorType::UniformBuffer,
                       .count = 1,
                       .stages = stages},
                      {.binding = 1,
                       .type = DescriptorType::StorageBuffer,
                       .count = 1,
                       .stages = stages},
                      {.binding = 2,
                       .type = DescriptorType::StorageBuffer,
                       .count = 1,
                       .stages = ShaderStageFlags::Fragment},
                      {.binding = 3,
                       .type = DescriptorType::StorageBuffer,
                       .count = 1,
                       .stages = ShaderStageFlags::Fragment}}});
    m_frameSet = device.CreateDescriptorSet({.layout = m_frameLayout});
    std::vector<DescriptorWrite> writes;
    const auto buffers = Buffers();
    for (uint32_t i = 0; i < buffers.size(); ++i)
      writes.push_back(
          {.binding = i, .resource = DescriptorBufferInfo{buffers[i]}});
    device.UpdateDescriptorSet(m_frameSet, writes);
    if (m_bindless)
    {
      if (textures.empty())
      {
        const std::array<std::byte, 4> white{std::byte{255}, std::byte{255},
                                             std::byte{255}, std::byte{255}};
        m_dummyImage = device.CreateImage(
            {.format = ImageFormat::RGBA8, .initialData = white});
        m_dummyView = device.CreateImageView(
            {.image = m_dummyImage, .format = ImageFormat::RGBA8});
        m_dummySampler = device.CreateSampler({});
        textures.push_back({m_dummyView, m_dummySampler});
      }
      m_textureCount = std::max(1u, uint32_t(textures.size()));
      m_assetLayout = device.CreateDescriptorSetLayout(
          {.bindings = {{.binding = 0,
                         .type = DescriptorType::CombinedImageSampler,
                         .count = m_textureCount,
                         .stages = ShaderStageFlags::Fragment}}});
      m_assetSet = device.CreateDescriptorSet({.layout = m_assetLayout});
      writes.clear();
      for (uint32_t i = 0; i < textures.size(); ++i)
        writes.push_back(
            {.binding = 0, .arrayElement = i, .resource = textures[i]});
      device.UpdateDescriptorSet(m_assetSet, writes);
    }
  }
  catch (...)
  {
    Release();
    throw;
  }
}
void SceneBindings::Release()
{
  if (m_frameSet)
    m_device.DestroyDescriptorSet(m_frameSet);
  if (m_assetSet)
    m_device.DestroyDescriptorSet(m_assetSet);
  if (m_frameLayout)
    m_device.DestroyDescriptorSetLayout(m_frameLayout);
  if (m_assetLayout)
    m_device.DestroyDescriptorSetLayout(m_assetLayout);
  for (auto buffer : Buffers())
    if (buffer)
      m_device.DestroyBuffer(buffer);
  if (m_dummyView)
    m_device.DestroyImageView(m_dummyView);
  if (m_dummyImage)
    m_device.DestroyImage(m_dummyImage);
  if (m_dummySampler)
    m_device.DestroySampler(m_dummySampler);
}
SceneBindings::~SceneBindings()
{
  Release();
}
void SceneBindings::Update(const SceneViewData& view,
                           std::span<const SceneObjectData> objects)
{
  if (objects.size() != m_offsets.size())
    throw std::invalid_argument(
        "Scene object table size changed without rebuilding");
  const auto write = [&](BufferHandle buffer, std::span<const std::byte> bytes)
  {
    auto mapped = m_device.MapMemory(buffer);
    std::memcpy(mapped.data(), bytes.data(), bytes.size());
    m_device.FlushMappedMemoryRange(buffer, 0, bytes.size());
    m_device.UnmapMemory(buffer);
  };
  write(m_view, std::as_bytes(std::span(&view, 1)));
  if (!objects.empty())
    write(m_objects, std::as_bytes(objects));
}
} // namespace Lighthouse
