#include <Lighthouse/Extended/EmbeddedResources.hpp>
#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
#include <Lighthouse/Extended/Rendering/SceneRenderStore.hpp>
#include <Lighthouse/Extended/World/Entity/Light.hpp>
#include <Lighthouse/Extended/World/Entity/Transform.hpp>
#include <Lighthouse/Extended/World/HostBuffer.hpp>
#include <cmath>
#include <cstring>

namespace Lighthouse
{
DefaultForwardLightingProvider::DefaultForwardLightingProvider(
    std::shared_ptr<SceneRenderStore> store, Vec3 ambient)
    : m_store(std::move(store)), m_ambient(ambient)
{
  if (!m_store || glm::any(glm::lessThan(ambient, Vec3(0))) ||
      !std::isfinite(ambient.x) || !std::isfinite(ambient.y) ||
      !std::isfinite(ambient.z))
    throw std::invalid_argument("Invalid scene lighting configuration");
}
void DefaultForwardLightingProvider::Update(RenderUpdateContext& ctx)
{
  m_store->Update(ctx);
}
std::vector<LightData> DefaultForwardLightingProvider::GetLights() const
{
  std::vector<LightData> result;
  const auto finite = [](Vec3 v)
  { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
  const auto& registry = m_store->GetScene().Registry();
  for (auto entity : registry.view<Light, Transform>())
  {
    const auto& light = registry.get<Light>(entity);
    if (!light.enabled)
      continue;
    const auto world = m_store->GetWorldMatrix(entity);
    const Vec3 position(world[3]);
    const Vec3 axis = glm::mat3(world) * Vec3(0, 0, -1);
    if (static_cast<uint32_t>(light.type) > 2 || !finite(light.color) ||
        !finite(position) || !finite(axis) ||
        glm::any(glm::lessThan(light.color, Vec3(0))) ||
        !std::isfinite(light.intensity) || light.intensity < 0 ||
        glm::dot(axis, axis) < 1e-12f ||
        (light.type != LightType::Directional &&
         (!std::isfinite(light.range) || light.range <= 0)) ||
        (light.type == LightType::Spot &&
         (!std::isfinite(light.innerAngle) ||
          !std::isfinite(light.outerAngle) || light.innerAngle < 0 ||
          light.outerAngle < light.innerAngle || light.outerAngle <= 0 ||
          light.outerAngle >= 90)))
      throw std::invalid_argument("Invalid scene Light component");
    result.push_back({Vec4(light.color, light.intensity),
                      Vec4(position, light.range),
                      Vec4(glm::normalize(axis), float(light.type)),
                      Vec4(std::cos(glm::radians(light.innerAngle)),
                           std::cos(glm::radians(light.outerAngle)), 0, 0)});
  }
  return result;
}
void DefaultForwardLightingProvider::BuildGraph(ForwardRenderContext& ctx)
{
  constexpr std::size_t capacity = 256;
  struct State
  {
    IRenderDevice* device{};
    BufferHandle buffer{};
    ~State()
    {
      if (buffer)
        device->DestroyBuffer(buffer);
    }
  };
  auto state = std::make_shared<State>();
  state->device = &ctx.frame.technique.device;
  state->buffer = state->device->CreateBuffer(
      {.size = sizeof(LightingFrameData) + capacity * sizeof(LightData),
       .usage = BufferUsage::Storage,
       .memory = {.requiredAccess =
                      MemoryAccess::CPUWrite | MemoryAccess::GPURead}});
  const auto buffer = ctx.frame.technique.graph.Import(
      "Forward scene lights",
      ImportedRenderGraphBuffer::Desc{sizeof(LightingFrameData) +
                                          capacity * sizeof(LightData),
                                      BufferUsage::Storage},
      ImportedRenderGraphBuffer{state->buffer});
  struct Data
  {
    RenderGraphResource buffer;
  };
  const auto& pass = ctx.frame.technique.graph.AddPass<Data>(
      "Update forward scene lights",
      [buffer](RenderGraph::Builder& builder, Data& data)
      { data.buffer = builder.Write(buffer); },
      [this, state](const Data&, FrameGraphPassResources&,
                    RenderGraphExecutePassContext& execute)
      {
        const auto lights = GetLights();
        if (lights.size() > capacity)
          throw std::runtime_error(
              "Default forward lighting supports up to 256 enabled lights");
        LightingFrameData header;
        header.ambientRadiance = Vec4(m_ambient, 0);
        header.lightCount[0] = lights.size();
        std::vector<std::byte> bytes(sizeof(header) +
                                     lights.size() * sizeof(LightData));
        std::memcpy(bytes.data(), &header, sizeof(header));
        if (!lights.empty())
          std::memcpy(bytes.data() + sizeof(header), lights.data(),
                      lights.size() * sizeof(LightData));
        WriteRetiredHostBuffer(*state->device, state->buffer, bytes);
      });
  ctx.lights.push_back(pass.buffer);
  ctx.lightingBuffer = state->buffer;
  auto source = ExtendedEmbedded::ReadText(
      "materials/passes/Lit.deferred.EvaluateLight.glsl");
  source.replace(source.find("set=1"), 5, "set=2");
  source += R"glsl(
// Incident light only. Lit's material shading model owns the BSDF evaluation.
uint ForwardLightCount() { return sceneLighting.lightCount.x; }
LightSample ForwardLightAt(uint index,vec3 position) {
    return EvaluateLight(sceneLighting.lights[index],position);
}
vec3 ForwardAmbientRadiance() { return sceneLighting.ambientRadiance.rgb; }
)glsl";
  ctx.lightingShader = std::move(source);
}
} // namespace Lighthouse
