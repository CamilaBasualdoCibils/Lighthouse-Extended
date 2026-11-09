#pragma once
#include <Lighthouse/Extended/Materials/Materials.hpp>
#include <Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>
#include <entt/entity/entity.hpp>
#include <map>

namespace Lighthouse
{
struct EntityGeometry
{
  Lighthouse::RenderGraphResource vertices, indices, uniform, parameters;
  Lighthouse::RenderMeshHandle mesh;
  entt::entity entity;
  Lighthouse::DescriptorSetHandle set;
  std::shared_ptr<const MaterialInstance> instance;
};
struct GeometryGroup
{
  std::shared_ptr<const ShadingModel> model;
  std::vector<EntityGeometry> geometry;
};
using PreparedGeometry = std::map<SurfaceModel, GeometryGroup>;

inline Lighthouse::RenderGraphResource
ImportGeometryBuffer(Lighthouse::TechniqueBuildContext& context,
                     const char* name, Lighthouse::BufferHandle buffer)
{
  const auto desc = context.device.GetBufferDesc(buffer);
  return context.graph.Import(name,
                              Lighthouse::ImportedRenderGraphBuffer::Desc{
                                  .size = desc->size, .usage = desc->usage},
                              Lighthouse::ImportedRenderGraphBuffer{buffer});
}
} // namespace Lighthouse
