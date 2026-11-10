#include <Lighthouse/Extended/Rendering/IRenderOutput.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include <unordered_set>

namespace Lighthouse
{
void RenderViewFamily::Validate() const
{
  if (views.empty()) throw std::invalid_argument("Render view family is empty");
  std::unordered_set<RenderViewID> ids;
  for (const auto& view : views)
    if (!view.id || !ids.insert(view.id).second || !view.resolution.x || !view.resolution.y)
      throw std::invalid_argument("Views require unique nonzero IDs and nonzero resolutions");
  if (grouping == RenderViewGrouping::Stereo &&
      (views.size() != 2 || views[0].role != RenderViewRole::LeftEye ||
       views[1].role != RenderViewRole::RightEye))
    throw std::invalid_argument("Stereo families require left-eye and right-eye views, in that order");
}
void ValidateRenderTargets(const RenderViewFamily& family, std::span<const RenderOutputTarget> targets)
{
  family.Validate();
  if (targets.size() != family.views.size())
    throw std::invalid_argument("Each logical view requires one output mapping");
  for (std::size_t i = 0; i < targets.size(); ++i)
    if (targets[i].view != family.views[i].id || targets[i].image == kInvalidGraphResource ||
        !targets[i].viewport.width || !targets[i].viewport.height)
      throw std::invalid_argument("Invalid render output mapping or view order");
}
RenderOutputTargets WindowRenderOutput::BuildTargets(TechniqueBuildContext& ctx, const RenderViewFamily& family)
{
  family.Validate();
  if (family.views.size() != 1)
    throw std::invalid_argument("A window region accepts one view; use separate regions for split screen");
  auto* window = ctx.renderer.GetView<WindowView>(m_window);
  if (!window) throw std::invalid_argument("Window output references an invalid window view");
  const auto size = window->GetWindow()->GetFramebufferSize();
  const auto region = m_region.value_or(RenderViewport{0, 0, size.width, size.height});
  if (!region.width || !region.height || region.x > size.width || region.y > size.height ||
      region.width > size.width - region.x || region.height > size.height - region.y)
    throw std::invalid_argument("Window output region is outside its framebuffer");
  const auto info = window->GetSwapchainInfo();
  RenderColorEncoding encoding;
  switch (info.format)
  {
  case SwapchainFormat::SRGB: encoding = RenderColorEncoding::SRGB; break;
  case SwapchainFormat::ScRGB: encoding = RenderColorEncoding::DisplayLinear; break;
  case SwapchainFormat::HDR10: encoding = RenderColorEncoding::BT2020PQ; break;
  }
  return {{family.views.front().id, window->GetAcquiredImageAsRGResource(), 0, 0,
           region, info.imageFormat, encoding}};
}
} // namespace Lighthouse
