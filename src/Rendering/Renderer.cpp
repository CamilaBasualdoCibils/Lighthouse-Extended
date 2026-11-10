#include <Lighthouse/Extended/Rendering/Renderer.hpp>
#include <algorithm>

namespace Lighthouse
{
RenderFeatureView::RenderFeatureView(std::shared_ptr<IRenderOutput> output)
{ SetOutput(std::move(output)); }
void RenderFeatureView::SetOutput(std::shared_ptr<IRenderOutput> output)
{
  if (!output) throw std::invalid_argument("Null render output");
  m_output = std::move(output);
  Invalidate();
}
void RenderFeatureView::SetViewSource(ViewSource source)
{
  if (!source) throw std::invalid_argument("Null render view source");
  m_source = std::move(source);
  Invalidate();
}
void RenderFeatureView::SetViewFamily(RenderViewFamily family)
{
  family.Validate();
  bool changed = family.grouping != m_family->grouping || family.views.size() != m_family->views.size();
  if (!changed)
    for (std::size_t i = 0; i < family.views.size(); ++i)
    {
      const auto& a = family.views[i]; const auto& b = m_family->views[i];
      changed |= a.id != b.id || a.role != b.role || a.resolution != b.resolution;
    }
  *m_family = std::move(family);
  if (changed) Invalidate();
}
void RenderFeatureView::Refresh() { if (m_source) SetViewFamily(m_source()); }
bool RenderFeatureView::NeedsRebuild() const noexcept
{
  return m_revision != m_compiledRevision ||
      (m_feature && m_feature->GetConfigurationRevision() != m_compiledFeatureRevision);
}
Renderer::Renderer(RenderGraphComposer* composer) : m_composer(composer)
{
  if (!composer) throw std::invalid_argument("Null render graph composer");
}
Renderer::~Renderer()
{
  if (m_composer->GetRenderDevice()) m_composer->GetRenderDevice()->WaitIdle();
}
RenderFeatureView& Renderer::CreateView(std::shared_ptr<IRenderOutput> output)
{
  auto view = std::make_shared<RenderFeatureView>(std::move(output));
  auto& result = *view;
  m_views.push_back(std::move(view));
  m_topologyChanged = true;
  return result;
}
bool Renderer::RemoveView(RenderFeatureView& view)
{
  const auto found = std::find_if(m_views.begin(), m_views.end(),
                                 [&](const auto& value) { return value.get() == &view; });
  if (found == m_views.end()) return false;
  m_views.erase(found);
  m_topologyChanged = true;
  return true;
}
void Renderer::Update(float deltaTime)
{
  RenderUpdateContext ctx{m_resources, deltaTime, ++m_frameId};
  for (const auto& view : m_views)
  {
    view->Refresh();
    if (view->m_feature) view->m_feature->Update(ctx);
  }
}
bool Renderer::NeedsRebuild() const noexcept
{
  return m_topologyChanged || std::any_of(m_views.begin(), m_views.end(),
                                         [](const auto& view) { return view->NeedsRebuild(); });
}
void Renderer::BuildGraph(TechniqueBuildContext& ctx)
{
  // Existing graph callbacks and GPU work can still reference the previous feature.
  ctx.device.WaitIdle();
  ++m_buildId;
  std::vector<std::shared_ptr<void>> retained;
  std::map<RenderGraphResource, RenderGraphResource> destinationVersions;
  const auto resourceRoots = [&] {
    std::map<RenderGraphResource, RenderGraphResource> roots;
    for (const auto& resource : ctx.graph.GetInspectionData(true).resources)
      roots.emplace(resource.handle, resource.resource);
    return roots;
  };
  for (const auto& view : m_views)
  {
    view->Refresh();
    view->m_family->Validate();
    if (!view->m_feature) throw std::logic_error("Render view has no render feature");
    auto targets = view->m_output->BuildTargets(ctx, *view->m_family);
    ValidateRenderTargets(*view->m_family, targets);
    // Different regions can write successive versions of the same destination.
    std::vector<RenderGraphResource> destinationRoots;
    const auto roots = resourceRoots();
    for (auto& target : targets)
    {
      const auto root = roots.at(target.image);
      destinationRoots.push_back(root);
      if (const auto previous = destinationVersions.find(root); previous != destinationVersions.end())
        target.image = previous->second;
    }
    RenderFeatureContext frame{ctx, view->m_family, std::move(targets), m_buildId, retained};
    retained.push_back(view);
    retained.push_back(view->m_feature);
    retained.push_back(view->m_output);
    view->m_result = view->m_feature->BuildGraph(frame);
    ValidateRenderTargets(*view->m_family, view->m_result.color);
    const auto resultRoots = resourceRoots();
    for (std::size_t i = 0; i < destinationRoots.size(); ++i)
    {
      if (resultRoots.at(view->m_result.color[i].image) != destinationRoots[i])
        throw std::logic_error("Feature must resolve its final output into the supplied destination");
      destinationVersions[destinationRoots[i]] = view->m_result.color[i].image;
    }
    view->m_compiledRevision = view->m_revision;
    view->m_compiledFeatureRevision = view->m_feature->GetConfigurationRevision();
  }
  // The compiled graph owns its feature/provider snapshots, even after a switch.
  // Keep final outputs live, including offscreen/transient destinations.
  // This side-effect pass retains ownership and dependencies; it performs no rendering.
  struct LifetimeData { std::vector<RenderGraphResource> outputs; };
  ctx.graph.AddPass<LifetimeData>("Retain render feature configuration",
      [destinationVersions](RenderGraph::Builder& builder, LifetimeData& data) {
        for (const auto& [_, image] : destinationVersions)
          data.outputs.push_back(builder.Read(image));
        builder.SetSideEffect();
      },
      [retained = std::move(retained)](const LifetimeData&, FrameGraphPassResources&,
                                     RenderGraphExecutePassContext& execute) {
        execute.commandList->Begin();
        execute.commandList->End();
      });
  m_topologyChanged = false;
}
} // namespace Lighthouse
