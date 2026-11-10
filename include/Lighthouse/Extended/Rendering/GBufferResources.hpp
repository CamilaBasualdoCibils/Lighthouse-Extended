#pragma once
#include <Lighthouse/Extended/Materials/Materials.hpp>
#include <Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp>
#include <map>

namespace Lighthouse
{
struct GBufferResources
{
  std::map<GBufferAttribute, RenderGraphResource> attributes;
  RenderGraphResource depth{kInvalidGraphResource};
};
} // namespace Lighthouse
