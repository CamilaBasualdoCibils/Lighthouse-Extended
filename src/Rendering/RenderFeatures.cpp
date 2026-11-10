#include <Lighthouse/Extended/Rendering/RenderFeatures.hpp>

namespace Lighthouse
{
namespace
{
template<class T> void UpdateOne(const std::shared_ptr<T>& value, RenderUpdateContext& ctx)
{ if (value) value->Update(ctx); }
template<class T> void UpdateAll(const std::vector<std::shared_ptr<T>>& values, RenderUpdateContext& ctx)
{ for (const auto& value : values) UpdateOne(value, ctx); }
template<class T, class Context> void BuildOne(const std::shared_ptr<T>& value, Context& ctx)
{
  if (!value) return;
  ctx.frame.keepAlive.push_back(value);
  value->BuildGraph(ctx);
}
template<class T, class Context> void BuildAll(const std::vector<std::shared_ptr<T>>& values, Context& ctx)
{ for (const auto& value : values) BuildOne(value, ctx); }
}
void RenderFeatureBase::Update(RenderUpdateContext& ctx)
{
  UpdateAll(m_preparation, ctx);
  UpdateAll(m_post, ctx);
  UpdateOne(m_resolver, ctx);
}
void RenderFeatureBase::Prepare(RenderFeatureContext& ctx)
{
  for (const auto& provider : m_preparation)
  {
    Hold(ctx, provider);
    provider->BuildGraph(ctx);
  }
}
void RenderFeatureBase::Finish(RenderFeatureContext& ctx, RenderFeatureResult& result)
{
  for (const auto& effect : m_post)
  {
    for (const auto input : effect->RequiredInputs())
    {
      const auto found = result.auxiliary.find(input);
      if (found == result.auxiliary.end())
        throw std::logic_error("Post-process requires an unavailable render output");
      ValidateRenderTargets(*ctx.family, found->second);
    }
    Hold(ctx, effect);
    effect->BuildGraph(ctx, result);
  }
  if (m_resolver)
  {
    Hold(ctx, m_resolver);
    result.color = m_resolver->BuildGraph(ctx, result);
  }
  ValidateRenderTargets(*ctx.family, result.color);
  // Graph versions can change, but destination subresources and their encoding cannot.
  for (std::size_t i = 0; i < result.color.size(); ++i)
  {
    const auto& output = result.color[i];
    const auto& target = ctx.destinations[i];
    if (output.arrayLayer != target.arrayLayer || output.mipLevel != target.mipLevel ||
        output.viewport.x != target.viewport.x || output.viewport.y != target.viewport.y ||
        output.viewport.width != target.viewport.width || output.viewport.height != target.viewport.height ||
        output.format != target.format || output.encoding != target.encoding)
      throw std::logic_error("Render output does not match destination; provide an output resolver");
  }
}
void DeferredRenderFeature::Update(RenderUpdateContext& ctx)
{
  RenderFeatureBase::Update(ctx);
  UpdateAll(m_geometry, ctx); UpdateOne(m_shadows, ctx); UpdateOne(m_lighting, ctx);
  UpdateOne(m_reflections, ctx); UpdateAll(m_compositors, ctx); UpdateAll(m_post, ctx);
}
RenderFeatureResult DeferredRenderFeature::BuildGraph(RenderFeatureContext& frame)
{
  Prepare(frame);
  DeferredRenderContext ctx{frame, std::vector<GBufferResources>(frame.family->views.size()),
                             RenderFeatureResult{frame.destinations}};
  BuildAll(m_geometry, ctx);
  BuildOne(m_shadows, ctx);
  BuildOne(m_lighting, ctx);
  // Reflection providers can read the lit scene and composite their contribution.
  BuildOne(m_reflections, ctx);
  BuildAll(m_compositors, ctx);
  BuildAll(m_post, ctx);
  Finish(frame, ctx.output);
  return std::move(ctx.output);
}
void ForwardRenderFeature::Update(RenderUpdateContext& ctx)
{
  RenderFeatureBase::Update(ctx);
  UpdateAll(m_geometry, ctx); UpdateOne(m_shadows, ctx); UpdateOne(m_lighting, ctx);
  UpdateOne(m_reflections, ctx); UpdateAll(m_compositors, ctx); UpdateAll(m_post, ctx);
}
RenderFeatureResult ForwardRenderFeature::BuildGraph(RenderFeatureContext& frame)
{
  Prepare(frame);
  ForwardRenderContext ctx{frame, RenderFeatureResult{frame.destinations}};
  // Forward lighting supplies data/shader integration to geometry providers.
  BuildOne(m_shadows, ctx);
  BuildOne(m_lighting, ctx);
  BuildLightAssignment(ctx);
  BuildAll(m_geometry, ctx);
  BuildOne(m_reflections, ctx);
  BuildAll(m_compositors, ctx);
  BuildAll(m_post, ctx);
  Finish(frame, ctx.output);
  return std::move(ctx.output);
}
void ForwardPlusRenderFeature::Update(RenderUpdateContext& ctx)
{ ForwardRenderFeature::Update(ctx); UpdateOne(m_assignment, ctx); }
void ForwardPlusRenderFeature::BuildLightAssignment(ForwardRenderContext& ctx)
{ BuildOne(m_assignment, ctx); }
void PathTracingRenderFeature::Update(RenderUpdateContext& ctx)
{
  RenderFeatureBase::Update(ctx);
  UpdateAll(m_geometry, ctx); UpdateOne(m_lightSampling, ctx); UpdateOne(m_integrator, ctx);
  UpdateOne(m_denoiser, ctx); UpdateAll(m_compositors, ctx); UpdateAll(m_post, ctx);
}
RenderFeatureResult PathTracingRenderFeature::BuildGraph(RenderFeatureContext& frame)
{
  Prepare(frame);
  PathTracingRenderContext ctx{frame, {}, RenderFeatureResult{frame.destinations}};
  BuildAll(m_geometry, ctx);
  BuildOne(m_lightSampling, ctx);
  BuildOne(m_integrator, ctx);
  BuildOne(m_denoiser, ctx);
  BuildAll(m_compositors, ctx);
  BuildAll(m_post, ctx);
  Finish(frame, ctx.output);
  return std::move(ctx.output);
}
} // namespace Lighthouse
