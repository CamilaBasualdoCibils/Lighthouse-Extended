#include <Lighthouse/Extended/Rendering/ForwardRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/DeferredRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/PathTracingRenderFeature.hpp>
#include <Lighthouse/Extended/Rendering/DeferredRendering.hpp>
#include "SceneColorResolver.hpp"
#include <set>
#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
#include <Lighthouse/Extended/Rendering/EnvironmentProviders.hpp>

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
void ValidateDestination(RenderFeatureContext& frame, const RenderFeatureResult& result)
{
  ValidateRenderTargets(*frame.family, result.color);
  for (std::size_t i = 0; i < result.color.size(); ++i)
  {
    const auto& output = result.color[i];
    const auto& target = frame.destinations[i];
    if (output.arrayLayer != target.arrayLayer || output.mipLevel != target.mipLevel ||
        output.viewport.x != target.viewport.x || output.viewport.y != target.viewport.y ||
        output.viewport.width != target.viewport.width || output.viewport.height != target.viewport.height ||
        output.format != target.format || output.encoding != target.encoding)
      throw std::logic_error("Render output does not match destination");
  }
}

}
void DeferredRenderFeature::Update(RenderUpdateContext& ctx)
{
  UpdateOne(m_environment,ctx);UpdateOne(m_indirect,ctx);
  UpdateAll(m_preparation, ctx);
  UpdateAll(m_geometry, ctx); UpdateOne(m_shadows, ctx); UpdateOne(m_lighting, ctx);
  UpdateOne(m_reflections, ctx); UpdateAll(m_compositors, ctx); UpdateAll(m_post, ctx);
}
RenderFeatureResult DeferredRenderFeature::BuildGraph(RenderFeatureContext& frame)
{
  DeferredRenderContext ctx{frame, std::vector<GBufferResources>(frame.family->views.size()),
                             DeferredRenderResult{RenderFeatureResult{frame.destinations}}};
  std::set<DeferredSemantics> attachments;
  std::set<DeferredHistory> histories;
  auto request=[&](const auto& provider){if(!provider)return;const auto needed=provider->GetDeferredDependencies();attachments.insert(needed.attachments.begin(),needed.attachments.end());histories.insert(needed.history.begin(),needed.history.end());};
  request(m_environment);request(m_indirect);request(m_lighting);request(m_shadows);request(m_reflections);
  for(const auto& provider:m_preparation)request(provider);
  for(const auto& provider:m_geometry)request(provider);
  for(const auto& provider:m_compositors)request(provider);
  for(const auto& provider:m_post)request(provider);
  ctx.requirements={{attachments.begin(),attachments.end()},{histories.begin(),histories.end()}};
  ctx.rendering=std::make_shared<DeferredRenderServices>(frame.technique.device,ctx.requirements);
  Hold(frame,ctx.rendering);
  if(!attachments.empty())ctx.rendering->BeginGeometry(ctx);
  BuildAll(m_preparation, ctx);
  for(auto history:histories) {
    const auto found=ctx.output.history.find(history);
    if(found==ctx.output.history.end())throw std::logic_error("Deferred temporal image history must be supplied by a preparation provider");
    ValidateRenderTargets(*frame.family,found->second);
  }
  if(m_environment && m_environment->GetDeferredSky()){
    Hold(frame,m_environment);ctx.rendering->BeginGeometry(ctx);
    m_environment->GetDeferredSky()->BuildGraphDeferred(ctx);
  }
  BuildAll(m_geometry, ctx);
  BuildOne(m_shadows, ctx);
  BuildOne(m_indirect, ctx);
  BuildOne(m_lighting, ctx);
  if(!m_lighting && !ctx.output.color.empty() && ctx.output.color.front().encoding==RenderColorEncoding::SceneLinear)
    ctx.rendering->Shade(ctx);
  // Reflection providers can read the lit scene and composite their contribution.
  BuildOne(m_reflections, ctx);
  BuildAll(m_compositors, ctx);
  for (const auto& post : m_post)
  {
    const auto required = post->GetDeferredDependencies();
    for (auto semantic : required.attachments)
    {
      const auto found = ctx.output.auxiliary.find(semantic);
      if (found == ctx.output.auxiliary.end())
        throw std::logic_error("Deferred post-process requires an unavailable attachment");
      ValidateRenderTargets(*frame.family, found->second);
    }
    for (auto history : required.history)
    {
      const auto found = ctx.output.history.find(history);
      if (found == ctx.output.history.end())
        throw std::logic_error("Deferred post-process requires unavailable history");
      ValidateRenderTargets(*frame.family, found->second);
    }
    Hold(frame, post);
    post->BuildGraphDeferred(ctx);
  }
  if(!ctx.output.color.empty() && ctx.output.color.front().encoding==RenderColorEncoding::SceneLinear)
    ResolveSceneColor(frame,ctx.output);
  ValidateDestination(frame, ctx.output);
  return std::move(ctx.output);
}
void ForwardRenderFeature::Update(RenderUpdateContext& ctx)
{
  UpdateAll(m_preparation, ctx);
  UpdateOne(m_environment, ctx); UpdateOne(m_indirect, ctx);
  UpdateAll(m_geometry, ctx); UpdateOne(m_shadows, ctx); UpdateOne(m_lighting, ctx);
  UpdateOne(m_reflections, ctx); UpdateAll(m_compositors, ctx); UpdateAll(m_post, ctx);
}
RenderFeatureResult ForwardRenderFeature::BuildGraph(RenderFeatureContext& frame)
{
  ForwardRenderContext ctx{frame, ForwardRenderResult{RenderFeatureResult{frame.destinations}}};
  BuildAll(m_preparation, ctx);
  BuildOne(m_shadows, ctx);
  BuildOne(m_lighting, ctx);
  BuildLightAssignment(ctx);
  BuildOne(m_indirect, ctx);
  m_rendering=std::make_shared<ForwardRenderServices>(frame.technique.device,ctx.lightingShader, ctx.indirectShader, ctx.indirectLayout, ctx.indirectSet);
  ctx.rendering=m_rendering; Hold(frame,m_rendering);
  if (m_environment && m_environment->GetForwardSky()) {
    Hold(frame, m_environment);
    m_rendering->BeginGeometry(ctx);
    m_environment->GetForwardSky()->BuildGraphForward(ctx);
  }
  BuildAll(m_geometry, ctx);
  BuildOne(m_reflections, ctx);
  BuildAll(m_compositors, ctx);
  for (const auto& post : m_post)
  {
    Hold(frame, post);
    post->BuildGraphForward(ctx);
  }
  ResolveDefault(frame, ctx.output);
  ValidateDestination(frame, ctx.output);
  return std::move(ctx.output);
}
namespace
{
template<class T> void Revision(std::uint64_t& revision, const std::shared_ptr<T>& provider)
{
  if (provider)
    revision = revision * 1099511628211ull ^ provider->GetConfigurationRevision();
}
template<class T> void Revision(std::uint64_t& revision, const std::vector<std::shared_ptr<T>>& providers)
{
  for (const auto& provider : providers) Revision(revision, provider);
}
}
std::uint64_t ForwardRenderFeature::GetConfigurationRevision() const noexcept
{
  auto revision = m_revision;
  Revision(revision, m_preparation); Revision(revision, m_environment); Revision(revision, m_indirect);
  Revision(revision, m_geometry); Revision(revision, m_shadows); Revision(revision, m_lighting);
  Revision(revision, m_reflections); Revision(revision, m_compositors); Revision(revision, m_post);
  return revision;
}
std::uint64_t DeferredRenderFeature::GetConfigurationRevision() const noexcept
{
  auto revision = m_revision;
  Revision(revision,m_environment);Revision(revision,m_indirect);
  Revision(revision, m_preparation);
  Revision(revision, m_geometry); Revision(revision, m_shadows); Revision(revision, m_lighting);
  Revision(revision, m_reflections); Revision(revision, m_compositors); Revision(revision, m_post);
  return revision;
}
std::uint64_t PathTracingRenderFeature::GetConfigurationRevision() const noexcept
{
  auto revision = m_revision;
  Revision(revision, m_geometry); Revision(revision, m_integrator); Revision(revision, m_lightSampling);
  Revision(revision, m_denoiser); Revision(revision, m_compositors); Revision(revision, m_post);
  return revision;
}
std::uint64_t ForwardPlusRenderFeature::GetConfigurationRevision() const noexcept
{
  auto revision = ForwardRenderFeature::GetConfigurationRevision();
  Revision(revision, m_assignment);
  return revision;
}
void ForwardRenderFeature::ResolveDefault(RenderFeatureContext& frame,RenderFeatureResult& result)
{
  if(m_rendering && !result.color.empty() && result.color.front().encoding==RenderColorEncoding::SceneLinear)
    m_rendering->Resolve(frame,result);
}
void ForwardPlusRenderFeature::Update(RenderUpdateContext& ctx)
{ ForwardRenderFeature::Update(ctx); UpdateOne(m_assignment, ctx); }
void ForwardPlusRenderFeature::BuildLightAssignment(ForwardRenderContext& ctx)
{ BuildOne(m_assignment, ctx); }
void PathTracingRenderFeature::Update(RenderUpdateContext& ctx)
{
  UpdateAll(m_geometry, ctx); UpdateOne(m_lightSampling, ctx); UpdateOne(m_integrator, ctx);
  UpdateOne(m_denoiser, ctx); UpdateAll(m_compositors, ctx); UpdateAll(m_post, ctx);
}
RenderFeatureResult PathTracingRenderFeature::BuildGraph(RenderFeatureContext& frame)
{
  PathTracingRenderContext ctx{frame, {}, RenderFeatureResult{frame.destinations}};
  BuildAll(m_geometry, ctx);
  BuildOne(m_lightSampling, ctx);
  BuildOne(m_integrator, ctx);
  BuildOne(m_denoiser, ctx);
  BuildAll(m_compositors, ctx);
  BuildAll(m_post, ctx);
  ValidateDestination(frame, ctx.output);
  return std::move(ctx.output);
}
} // namespace Lighthouse
