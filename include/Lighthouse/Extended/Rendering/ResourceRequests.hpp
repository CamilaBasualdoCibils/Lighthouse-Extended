#pragma once
#include <Lighthouse/Engine/Core/Handle.hpp>
#include <Lighthouse/Extended/Resources/Material.hpp>
#include <Lighthouse/Extended/Resources/Model.hpp>

namespace Lighthouse
{
struct RenderObjectTag;
using RenderObjectHandle = Handle<RenderObjectTag>;

// Interface only. No resource resolution, allocation or GPU loading yet.
class ResourceRequests
{
public:
  RenderObjectHandle LoadMesh(const ModelHandle&) { return {}; }
  RenderObjectHandle LoadMaterial(const MaterialHandle&) { return {}; }
  RenderObjectHandle LoadTexture(const TextureHandle&) { return {}; }
  RenderObjectHandle LoadImage(const Image&) { return {}; }
};
} // namespace Lighthouse
