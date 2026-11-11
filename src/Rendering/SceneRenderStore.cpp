#include <Lighthouse/Extended/Rendering/SceneRenderStore.hpp>
#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
#include <Lighthouse/Extended/World/Entity/Transform.hpp>
#include <fstream>
#include <cstring>
#include <numeric>
#include <set>
#include <tuple>

namespace Lighthouse
{
SceneMeshGPU::~SceneMeshGPU()
{
  if (vertices) device->DestroyBuffer(vertices);
  if (indices) device->DestroyBuffer(indices);
}
SceneImageGPU::~SceneImageGPU()
{
  if (view) device->DestroyImageView(view);
  if (image) device->DestroyImage(image);
}
SceneTextureGPU::~SceneTextureGPU() { if (sampler) device->DestroySampler(sampler); }
namespace
{
std::vector<std::byte> ReadFile(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error("Cannot read render resource: " + path.string());
  const auto length = file.tellg();
  if (length < 0) throw std::runtime_error("Cannot determine resource length");
  std::vector<std::byte> bytes(static_cast<std::size_t>(length));
  file.seekg(0);
  if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
    throw std::runtime_error("Short read: " + path.string());
  return bytes;
}
template<class T> T ReadScalar(const std::byte* bytes) { T value; std::memcpy(&value, bytes, sizeof(T)); return value; }
std::size_t ComponentSize(AccessorComponentType type)
{
  switch (type) {
  case AccessorComponentType::int8: case AccessorComponentType::uint8: return 1;
  case AccessorComponentType::int16: case AccessorComponentType::uint16: return 2;
  default: return 4;
  }
}
double Number(const std::byte* bytes, AccessorComponentType type, bool normalized)
{
  switch (type) {
  case AccessorComponentType::float32: return ReadScalar<float>(bytes);
  case AccessorComponentType::uint8: return normalized ? ReadScalar<uint8_t>(bytes) / 255.0 : ReadScalar<uint8_t>(bytes);
  case AccessorComponentType::uint16: return normalized ? ReadScalar<uint16_t>(bytes) / 65535.0 : ReadScalar<uint16_t>(bytes);
  case AccessorComponentType::uint32: return ReadScalar<uint32_t>(bytes);
  case AccessorComponentType::int8: return normalized ? std::max(-1.0, ReadScalar<int8_t>(bytes) / 127.0) : ReadScalar<int8_t>(bytes);
  case AccessorComponentType::int16: return normalized ? std::max(-1.0, ReadScalar<int16_t>(bytes) / 32767.0) : ReadScalar<int16_t>(bytes);
  case AccessorComponentType::int32: return ReadScalar<int32_t>(bytes);
  }
  throw std::runtime_error("Unsupported accessor component");
}
}

struct SceneRenderStore::GPUCache
{
  template<class T> struct Cached { std::uint64_t revision{}; std::shared_ptr<T> object; };
  using Key = std::pair<std::uint64_t, std::uint64_t>;
  std::map<Key, Cached<SceneMeshGPU>> meshes;
  std::map<Key, Cached<SceneMaterialGPU>> materials;
  std::map<Key, Cached<SceneImageGPU>> images;
  std::map<Key, Cached<SceneTextureGPU>> textures;
  std::map<std::pair<std::uint64_t, std::array<unsigned char, 4>>, std::shared_ptr<SceneTextureGPU>> defaults;
  std::map<std::pair<std::shared_ptr<const GLTFAsset>, std::size_t>, std::vector<std::byte>> buffers;

  const std::vector<std::byte>& Buffer(std::shared_ptr<const GLTFAsset> asset, std::size_t index)
  {
    const auto key = std::make_pair(asset, index);
    if (const auto found = buffers.find(key); found != buffers.end()) return found->second;
    const auto& buffer = asset->buffers.at(index);
    auto bytes = std::visit([](const auto& source) -> std::vector<std::byte> {
      using T = std::decay_t<decltype(source)>;
      if constexpr (std::is_same_v<T, BufferAsset::RawData>) return source.data;
      else { auto data = ReadFile(source.path); if (data.size() < source.size) throw std::runtime_error("Short glTF buffer"); return data; }
    }, buffer.data);
    return buffers.emplace(key, std::move(bytes)).first->second;
  }
  glm::dvec4 Element(std::shared_ptr<const GLTFAsset> asset, std::uint32_t index, std::size_t element, std::size_t components)
  {
    const auto& accessor = asset->accesors.at(index);
    if (accessor.bufferView < 0 || accessor.byteOffset < 0 || element >= accessor.count)
      throw std::runtime_error("Invalid mesh accessor");
    const auto& views = asset->bufferViews.at(accessor.bufferView).views;
    if (views.size() != 1) throw std::runtime_error("Mesh accessor requires one buffer view");
    const auto& view = views.front();
    const auto& bytes = Buffer(asset, view.buffer);
    const auto packed = components * ComponentSize(accessor.componentType);
    const std::size_t stride = view.byteStride.value_or(packed);
    if (stride && element > (SIZE_MAX - std::size_t(accessor.byteOffset)) / stride)
      throw std::runtime_error("Mesh accessor offset overflow");
    const std::size_t offset = std::size_t(accessor.byteOffset) + element * stride;
    if (stride < packed || offset > view.size || packed > view.size - offset ||
        std::size_t(view.offset) > bytes.size() || view.size > bytes.size() - view.offset)
      throw std::runtime_error("Mesh accessor is outside its buffer view");
    glm::dvec4 result(0);
    for (std::size_t i = 0; i < components; ++i)
      result[i] = Number(bytes.data() + view.offset + offset + i * ComponentSize(accessor.componentType),
                         accessor.componentType, accessor.normalized);
    return result;
  }
  Mesh3D Mesh(const ModelHandle& handle)
  {
    return std::visit([&](const auto& source) -> Mesh3D {
      using T = std::decay_t<decltype(source)>;
      if constexpr (std::is_same_v<T, OwnedModel>) return *source.mesh;
      else {
        const auto& p = source.asset->primitives.at(source.primitive);
        const auto& position = source.asset->accesors.at(p.position);
        if (position.type != AccessorType::Vec3 || position.count > UINT32_MAX)
          throw std::runtime_error("Invalid mesh positions");
        Mesh3D mesh;
        mesh.vertices.resize(position.count);
        for (std::size_t i = 0; i < position.count; ++i) {
          auto& v = mesh.vertices[i];
          v.position = Vec3(Element(source.asset, p.position, i, 3));
          if (p.normal != UINT32_MAX) v.normal = Vec3(Element(source.asset, p.normal, i, 3));
          if (const auto uv = p.texcoords.find(0); uv != p.texcoords.end()) v.uv = Vec2(Element(source.asset, uv->second, i, 2));
        }
        if (p.indices) {
          const auto& accessor = source.asset->accesors.at(*p.indices);
          if (accessor.count > UINT32_MAX) throw std::runtime_error("Mesh has too many indices");
          if (accessor.type != AccessorType::Scalar || accessor.normalized ||
              (accessor.componentType != AccessorComponentType::uint8 && accessor.componentType != AccessorComponentType::uint16 && accessor.componentType != AccessorComponentType::uint32))
            throw std::runtime_error("Invalid mesh index accessor");
          for (std::size_t i = 0; i < accessor.count; ++i) {
            // Read integers without a float conversion (large uint32 indices retain precision).
            const auto value = Element(source.asset, *p.indices, i, 1).x;
            if (value < 0 || value >= position.count) throw std::runtime_error("Mesh index out of range");
            mesh.indices.push_back(static_cast<std::uint32_t>(value));
          }
        } else { mesh.indices.resize(position.count); std::iota(mesh.indices.begin(), mesh.indices.end(), 0); }
        if (mesh.indices.size() % 3) throw std::runtime_error("Triangle mesh has incomplete triangles");
        if (p.normal == UINT32_MAX) {
          for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            auto& a = mesh.vertices[mesh.indices[i]]; auto& b = mesh.vertices[mesh.indices[i+1]]; auto& c = mesh.vertices[mesh.indices[i+2]];
            const auto normal = glm::cross(b.position - a.position, c.position - a.position);
            a.normal += normal; b.normal += normal; c.normal += normal;
          }
        }
        for (auto& v : mesh.vertices) v.normal = glm::dot(v.normal,v.normal) > 1e-12f ? glm::normalize(v.normal) : Vec3(0,1,0);
        return mesh;
      }
    }, handle.GetSource());
  }
  std::shared_ptr<SceneImageGPU> UploadImage(IRenderDevice& device, const ImageData& pixels)
  {
    auto result = std::make_shared<SceneImageGPU>(); result->device = &device; result->resolution = pixels.GetResolution();
    result->format=pixels.GetFormat(); result->mipLevels=pixels.GetMipCount();
    result->image = device.CreateImage(pixels.ToImageDesc());
    result->view = device.CreateImageView({.image=result->image, .format=pixels.GetFormat(), .mipLevelCount=pixels.GetMipCount()});
    return result;
  }
  std::shared_ptr<SceneTextureGPU> Default(IRenderDevice& device, std::array<unsigned char,4> pixel)
  {
    const auto key = std::make_pair(device.DeviceId(), pixel);
    if (const auto found=defaults.find(key); found!=defaults.end()) return found->second;
    std::vector<std::byte> bytes; for (auto v:pixel) bytes.push_back(std::byte(v));
    auto image = UploadImage(device, ImageData({1,1}, ImageFormat::RGBA8, std::move(bytes)));
    auto result = std::make_shared<SceneTextureGPU>(); result->device=&device;
    result->sourceImage=image; result->image=image->image; result->view=image->view; result->sampler=device.CreateSampler({});
    defaults.emplace(key,result); return result;
  }
  std::shared_ptr<SceneTextureGPU> Texture(IRenderDevice& device, const TextureHandle& texture)
  {
    const auto image=texture.GetImage();
    const Key key{device.DeviceId(),texture.GetID()};
    auto& cached=textures[key];
    if (cached.object && cached.revision==image.GetRevision()) return cached.object;
    auto& imageCache=images[{device.DeviceId(),image.GetID()}];
    if (!imageCache.object || imageCache.revision!=image.GetRevision()) {
      imageCache.object=UploadImage(device,image.Resolve()); imageCache.revision=image.GetRevision();
    }
    auto result=std::make_shared<SceneTextureGPU>(); result->device=&device;
    result->sourceImage=imageCache.object; result->image=result->sourceImage->image; result->view=result->sourceImage->view;
    auto sampler=texture.GetSampler();
    result->sampler=device.CreateSampler(sampler);
    cached={image.GetRevision(),result}; return result;
  }
};

SceneRenderStore::SceneRenderStore(Scene& scene) : m_gpu(std::make_unique<GPUCache>()), m_world(&scene) {}
SceneRenderStore::~SceneRenderStore()
{
  std::set<IRenderDevice*> devices;
  for (const auto& [_, cached] : m_gpu->meshes) if (cached.object) devices.insert(cached.object->device);
  for (const auto& [_, cached] : m_gpu->images) if (cached.object) devices.insert(cached.object->device);
  for (const auto& [_, texture] : m_gpu->defaults) devices.insert(texture->device);
  for (auto* device : devices) device->WaitIdle();
}
Mat4 SceneRenderStore::GetWorldMatrix(entt::entity entity) const
{
  Mat4 result(1); std::set<entt::entity> visited;
  while (m_world->Registry().valid(entity) && m_world->Registry().all_of<Transform>(entity)) {
    if (!visited.insert(entity).second) throw std::logic_error("Cycle in scene transform hierarchy");
    const auto& transform=m_world->Registry().get<Transform>(entity);
    result=transform.GetMatrix()*result;
    if (!transform.GetParent()) break;
    entity=*transform.GetParent();
  }
  return result;
}
std::vector<SceneRenderStore::Instance> SceneRenderStore::GetInstances() const
{
  std::vector<Instance> result;
  for (const auto& [entity, cached] : m_sceneDraws)
    if (cached.mesh && cached.material) result.push_back({entity,cached.mesh,cached.material,cached.transform});
  return result;
}
std::shared_ptr<SceneMeshGPU> SceneRenderStore::GetMesh(IRenderDevice& device, const ModelHandle& mesh)
{
  auto& cached=m_gpu->meshes[{device.DeviceId(),mesh.GetID()}];
  if (cached.object && cached.revision==mesh.GetRevision()) return cached.object;
  const auto cpu=m_gpu->Mesh(mesh); const auto description=cpu.ToRenderMesh();
  if (cpu.topology != PrimitiveTopology::TriangleList) throw std::runtime_error("Forward scene meshes currently require triangle lists");
  if (cpu.vertices.empty()) throw std::runtime_error("Cannot render an empty mesh");
  auto result=std::make_shared<SceneMeshGPU>(); result->device=&device; result->layout=description.layout;
  result->vertices=device.CreateBuffer({.size=description.vertexData.front().bytes.size(), .usage=BufferUsage::Vertex,
                                        .initialData=description.vertexData.front().bytes});
  auto indices=cpu.indices; if(indices.empty()){indices.resize(cpu.vertices.size());std::iota(indices.begin(),indices.end(),0);}
  result->indices=device.CreateBuffer({.size=indices.size()*sizeof(uint32_t), .usage=BufferUsage::Index, .initialData=AsBytes(indices)});
  result->indexCount=indices.size(); cached={mesh.GetRevision(),result}; return result;
}
std::shared_ptr<SceneMaterialGPU> SceneRenderStore::GetMaterial(IRenderDevice& device, const MaterialHandle& material)
{
  std::uint64_t revision=material.GetRevision();
  for(const auto& [_,texture]:material.GetTextures()) revision=revision*1099511628211ull ^ texture.GetImage().GetRevision();
  auto& cached=m_gpu->materials[{device.DeviceId(),material.GetID()}];
  if(cached.object && cached.revision==revision) return cached.object;
  auto result=std::make_shared<SceneMaterialGPU>();
  const auto source=material.GetSource();
  if (const auto* custom=std::get_if<DefinedMaterial>(&source)) {
    result->definition=custom->path;
    result->options.parameters=custom->parameters;
    for (const auto& [name,texture] : material.GetTextures()) {
      auto resolved=m_gpu->Texture(device,texture);
      result->options.textures[name]={resolved->view,resolved->sampler};
      result->resolvedTextures[name]=std::move(resolved);
    }
    cached={revision,result}; return result;
  }
  if (const auto* owned=std::get_if<OwnedMaterial>(&source)) result->data=owned->data;
  else {const auto& gltf=std::get<GLTFMaterial>(source);result->data=gltf.asset->materials.at(gltf.material);}
  result->normalMapped=material.GetTextures().contains("normal");
  const std::array<const char*,5> names{"baseColor","metallicRoughness","normal","occlusion","emissive"};
  for(std::size_t i=0;i<names.size();++i) {
    const auto found=material.GetTextures().find(names[i]);
    result->textures[i]=found==material.GetTextures().end()
      ? m_gpu->Default(device,i==2?std::array<unsigned char,4>{128,128,255,255}:std::array<unsigned char,4>{255,255,255,255})
      : m_gpu->Texture(device,found->second);
  }
  for(const auto* info:{result->data.baseColorTexture?&*result->data.baseColorTexture:nullptr,
                       result->data.metallicRoughnessTexture?&*result->data.metallicRoughnessTexture:nullptr,
                       result->data.emissiveTexture?&*result->data.emissiveTexture:nullptr})
    if(info && info->texCoord!=0) throw std::runtime_error("Forward scene materials currently support TEXCOORD_0 only");
  if ((result->data.normalTexture && result->data.normalTexture->texCoord != 0) ||
      (result->data.occlusionTexture && result->data.occlusionTexture->texCoord != 0))
    throw std::runtime_error("Forward scene materials currently support TEXCOORD_0 only");
  const auto& data=result->data;
  result->options.parameters={
    {"baseColorFactor",data.baseColorFactor},{"emissiveFactor",data.emissiveFactor},
    {"metallicFactor",data.metallicFactor},{"roughnessFactor",data.roughnessFactor},
    {"normalScale",data.normalTexture?data.normalTexture->scale:1.0f},
    {"occlusionStrength",data.occlusionTexture?data.occlusionTexture->strength:1.0f},
    {"alphaMode",int32_t(data.alphaMode)},{"alphaCutoff",data.alphaCutoff},
    {"doubleSided",data.doubleSided},{"hasNormal",result->normalMapped}};
  for (std::size_t i=0;i<names.size();++i) {
    const auto& texture=result->textures[i];
    result->resolvedTextures[names[i]]=texture;
    result->options.textures[names[i]]={texture->view,texture->sampler};
  }
  cached={revision,result}; return result;
}
} // namespace Lighthouse
