#include "RenderInfrastructureDevice.hpp"
#include <Lighthouse/Extended/Rendering/Renderer.hpp>
#include <Lighthouse/Extended/Rendering/Bloom.hpp>
#include <Lighthouse/Extended/Rendering/SceneRenderProviders.hpp>
#include <gtest/gtest.h>

using namespace Lighthouse;
namespace
{
RenderViewFamily Mono(RenderViewID id = 1)
{ return {{RenderViewData{.id = id, .resolution = {64, 64}}}}; }
class Surface final : public IRenderView
{
public:
  RenderGraphResource image{kInvalidGraphResource};
  void BuildGraph(ViewBuildContext& ctx) override
  {
    struct Data { RenderGraphResource image; };
    const auto& data = ctx.graph.AddPass<Data>("test surface",
      [](RenderGraph::Builder& builder, Data& data) {
        data.image = builder.Create<RenderGraphImage>("surface", {
            .width = 128, .height = 64, .format = ImageFormat::RGBA8,
            .usage = {ImageUsage::ColorAttachment}});
        data.image = builder.Write(data.image);
      }, [](const Data&, FrameGraphPassResources&, RenderGraphExecutePassContext&) {});
    image = data.image;
  }
};
std::shared_ptr<IRenderOutput> Output(RenderViewHandle surface, std::uint32_t x = 0)
{
  return std::make_shared<GraphRenderOutput>([surface, x](TechniqueBuildContext& ctx, const RenderViewFamily& family) {
    RenderOutputTargets targets;
    for (std::size_t i = 0; i < family.views.size(); ++i)
      targets.push_back({family.views[i].id, ctx.renderer.GetView<Surface>(surface)->image,
                         static_cast<std::uint32_t>(i), 0, {x, 0, 64, 64}});
    return targets;
  });
}
class TestComposer : public RenderGraphComposer
{
public:
  TestComposer() : RenderGraphComposer(Device()) {}
private:
  static std::unique_ptr<IRenderDevice> Device()
  {
    spdlog::drop("RenderGraphSimpleAllocator");
    return std::make_unique<TestRenderDevice>();
  }
};
struct Recorder : IDeferredGeometryProvider
{
  std::vector<int>& calls;
  int id;
  Recorder(std::vector<int>& c, int i) : calls(c), id(i) {}
  void Update(RenderUpdateContext&) override { calls.push_back(id * 10); }
  void BuildGraph(DeferredRenderContext&) override { calls.push_back(id); }
};
struct WriteTarget : IDeferredGeometryProvider
{
  RenderGraphResource& input;
  explicit WriteTarget(RenderGraphResource& value) : input(value) {}
  void BuildGraph(DeferredRenderContext& ctx) override
  {
    input = ctx.output.color.front().image;
    struct Data { RenderGraphResource image; };
    const auto& data = ctx.frame.technique.graph.AddPass<Data>("provider write",
      [image = input](RenderGraph::Builder& builder, Data& data) {
        data.image = builder.Write(builder.Read(image));
        builder.SetSideEffect();
      }, [](const Data&, FrameGraphPassResources&, RenderGraphExecutePassContext&) {});
    ctx.output.color.front().image = data.image;
  }
};
struct NeedsNormal : IDeferredPostProcess
{
  DeferredDependencies GetDeferredDependencies() const override { return {{DeferredSemantics::Normal}, {}}; }
  void BuildGraphDeferred(DeferredRenderContext&) override {}
};
struct TrackedLighting : IDeferredLightingProvider
{
  int& destroyed;
  explicit TrackedLighting(int& value) : destroyed(value) {}
  ~TrackedLighting() override { ++destroyed; }
  void BuildGraph(DeferredRenderContext&) override {}
};
struct ForwardRecorder : IForwardGeometryProvider
{
  int& calls;
  explicit ForwardRecorder(int& c) : calls(c) {}
  void BuildGraph(ForwardRenderContext&) override { ++calls; }
};
struct AssignmentRecorder : IForwardLightAssignmentProvider
{
  int& calls;
  explicit AssignmentRecorder(int& c) : calls(c) {}
  void BuildGraph(ForwardRenderContext&) override { ++calls; }
};
struct RayRecorder : IRaySceneProvider
{
  int& calls;
  explicit RayRecorder(int& c) : calls(c) {}
  void BuildGraph(PathTracingRenderContext&) override { ++calls; }
};
static_assert(!std::derived_from<SceneForwardRenderProvider, IDeferredGeometryProvider>);
static_assert(!std::derived_from<SceneRaytracedRenderProvider, IForwardGeometryProvider>);
}

TEST(RenderFeatures, MultipleProvidersAndIndependentFeatures)
{
  TestComposer composer;
  const auto surface = composer.CreateView<Surface>();
  Renderer renderer(&composer);
  auto& first = renderer.CreateView(Output(surface));
  first.SetViewFamily(Mono());
  auto& deferred = first.SetRenderFeature<DeferredRenderFeature>();
  std::vector<int> calls;
  deferred.AddProvider<Recorder>(calls, 1);
  deferred.AddProvider<Recorder>(calls, 2);
  auto& second = renderer.CreateView(Output(surface, 64));
  second.SetViewFamily(Mono(2));
  int rayCalls = 0;
  second.SetRenderFeature<PathTracingRenderFeature>().AddProvider<RayRecorder>(rayCalls);
  renderer.Update(0.016f);
  RenderGraph graph;
  ViewBuildContext viewContext{graph, *composer.GetRenderDevice()};
  composer.GetView<Surface>(surface)->BuildGraph(viewContext);
  TechniqueBuildContext context{graph, composer, *composer.GetRenderDevice()};
  renderer.BuildGraph(context);
  EXPECT_EQ(calls, (std::vector<int>{10, 20, 1, 2}));
  EXPECT_EQ(rayCalls, 1);
  EXPECT_FALSE(renderer.NeedsRebuild());
  EXPECT_EQ(first.GetResult().color[0].viewport.x, 0);
  EXPECT_EQ(second.GetResult().color[0].viewport.x, 64);
  graph.Compile();
}

TEST(RenderFeatures, SplitScreenChainsDestinationVersions)
{
  TestComposer composer;
  auto surface = composer.CreateView<Surface>();
  auto ref = composer.AddTechnique<Renderer>(&composer);
  auto* renderer = composer.GetTechnique<Renderer>(ref);
  auto& left = renderer->CreateView(Output(surface));
  left.SetViewFamily(Mono());
  RenderGraphResource leftInput, rightInput;
  left.SetRenderFeature<DeferredRenderFeature>().AddProvider<WriteTarget>(leftInput);
  auto& right = renderer->CreateView(Output(surface, 64));
  right.SetViewFamily(Mono(2));
  right.SetRenderFeature<DeferredRenderFeature>().AddProvider<WriteTarget>(rightInput);
  composer.Compile();
  EXPECT_NE(leftInput, rightInput);
  EXPECT_EQ(left.GetResult().color.front().image, rightInput);
}

TEST(RenderFeatures, StereoSupportsTwoLayersWithIndependentMatrices)
{
  TestComposer composer;
  const auto surface = composer.CreateView<Surface>();
  auto ref = composer.AddTechnique<Renderer>(&composer);
  auto* renderer = composer.GetTechnique<Renderer>(ref);
  auto& view = renderer->CreateView(Output(surface));
  RenderViewData left{.id = 7, .resolution = {64, 64}, .role = RenderViewRole::LeftEye};
  RenderViewData right{.id = 8, .resolution = {64, 64}, .role = RenderViewRole::RightEye};
  left.view[3].x = 0.032f;
  right.view[3].x = -0.032f;
  left.projection[2].x = 0.1f;
  right.projection[2].x = -0.1f;
  view.SetViewFamily({{left, right}, RenderViewGrouping::Stereo});
  view.SetRenderFeature<DeferredRenderFeature>();
  composer.Compile();
  const auto& targets = view.GetResult().color;
  ASSERT_EQ(targets.size(), 2);
  EXPECT_EQ(targets[0].image, targets[1].image);
  EXPECT_EQ(targets[0].arrayLayer, 0);
  EXPECT_EQ(targets[1].arrayLayer, 1);
  EXPECT_NE(view.GetViewFamily().views[0].ViewProjection(), view.GetViewFamily().views[1].ViewProjection());
}

TEST(RenderFeatures, SwitchingAndReplacingProvidersRetainsOldGraphOwnership)
{
  int destroyed = 0;
  TestComposer composer;
  auto surface = composer.CreateView<Surface>();
  auto ref = composer.AddTechnique<Renderer>(&composer);
  auto* renderer = composer.GetTechnique<Renderer>(ref);
  auto& view = renderer->CreateView(Output(surface));
  view.SetViewFamily(Mono());
  auto& deferred = view.SetRenderFeature<DeferredRenderFeature>();
  deferred.SetLighting<TrackedLighting>(destroyed);
  composer.Compile();
  deferred.SetLighting<TrackedLighting>(destroyed);
  EXPECT_TRUE(renderer->NeedsRebuild());
  EXPECT_EQ(destroyed, 0);
  composer.Compile();
  EXPECT_EQ(destroyed, 1);
  view.SetRenderFeature<PathTracingRenderFeature>();
  EXPECT_TRUE(renderer->NeedsRebuild());
  EXPECT_EQ(destroyed, 1);
  composer.Compile();
  EXPECT_EQ(destroyed, 2);
}

TEST(RenderFeatures, CameraPoseChangesDoNotRebuildButResolutionDoes)
{
  TestComposer composer;
  auto surface = composer.CreateView<Surface>();
  auto ref = composer.AddTechnique<Renderer>(&composer);
  auto* renderer = composer.GetTechnique<Renderer>(ref);
  auto& view = renderer->CreateView(Output(surface));
  view.SetRenderFeature<ForwardRenderFeature>();
  auto family = Mono();
  view.SetViewSource([&] { return family; });
  composer.Compile();
  family.views[0].view[3].x = 5;
  renderer->Update(0.016f);
  EXPECT_FALSE(renderer->NeedsRebuild());
  EXPECT_EQ(view.GetViewFamily().views[0].view[3].x, 5);
  family.views[0].resolution = {32, 32};
  renderer->Update(0.016f);
  EXPECT_TRUE(renderer->NeedsRebuild());
}

TEST(RenderFeatures, RejectsMissingPostProcessInputs)
{
  TestComposer composer;
  auto surface = composer.CreateView<Surface>();
  auto ref = composer.AddTechnique<Renderer>(&composer);
  auto* renderer = composer.GetTechnique<Renderer>(ref);
  auto& view = renderer->CreateView(Output(surface));
  view.SetViewFamily(Mono());
  view.SetRenderFeature<DeferredRenderFeature>().AddDeferredPostProcess<NeedsNormal>();
  EXPECT_THROW(composer.Compile(), std::logic_error);
}

TEST(RenderFeatures, ForwardPlusRunsAssignmentAndGeometry)
{
  TestComposer composer;
  auto surface = composer.CreateView<Surface>();
  auto ref = composer.AddTechnique<Renderer>(&composer);
  auto* renderer = composer.GetTechnique<Renderer>(ref);
  auto& view = renderer->CreateView(Output(surface));
  view.SetViewFamily(Mono());
  int geometry = 0, assignment = 0;
  auto& feature = view.SetRenderFeature<ForwardPlusRenderFeature>();
  feature.SetLightAssignment<AssignmentRecorder>(assignment);
  feature.AddProvider<ForwardRecorder>(geometry);
  composer.Compile();
  EXPECT_EQ(assignment, 1);
  EXPECT_EQ(geometry, 1);
}

TEST(RenderFeatures, InvalidFamiliesAndMappingsAreRejected)
{
  EXPECT_THROW(RenderViewFamily{}.Validate(), std::invalid_argument);
  auto family = Mono();
  family.views.push_back(family.views.front());
  EXPECT_THROW(family.Validate(), std::invalid_argument);
  family = Mono();
  family.grouping = RenderViewGrouping::Stereo;
  EXPECT_THROW(family.Validate(), std::invalid_argument);
  family = Mono();
  EXPECT_THROW(ValidateRenderTargets(family, {}), std::invalid_argument);
  const RenderOutputTargets invalid{{1, kInvalidGraphResource, 0, 0, {0, 0, 64, 64}}};
  EXPECT_THROW(ValidateRenderTargets(family, invalid), std::invalid_argument);
}

TEST(RenderFeatures, MultiFeaturePostProcessHasOneProviderAndRunsTypedEntryPoints)
{
  struct Both final : IForwardPostProcess, IDeferredPostProcess
  {
    int& updates; int& forward; int& deferred;
    Both(int& u,int& f,int& d):updates(u),forward(f),deferred(d){}
    void Update(RenderUpdateContext&) override { ++updates; }
    void BuildGraphForward(ForwardRenderContext&) override { ++forward; }
    void BuildGraphDeferred(DeferredRenderContext&) override { ++deferred; }
  };
  static_assert(std::derived_from<Both,IRenderProvider>);
  TestComposer composer;
  auto surface=composer.CreateView<Surface>();
  auto ref=composer.AddTechnique<Renderer>(&composer);
  auto* renderer=composer.GetTechnique<Renderer>(ref);
  int updates=0,forward=0,deferred=0;
  auto& first=renderer->CreateView(Output(surface));first.SetViewFamily(Mono());
  first.SetRenderFeature<ForwardRenderFeature>().AddForwardPostProcess<Both>(updates,forward,deferred);
  auto& second=renderer->CreateView(Output(surface,64));second.SetViewFamily(Mono(2));
  second.SetRenderFeature<DeferredRenderFeature>().AddDeferredPostProcess<Both>(updates,forward,deferred);
  renderer->Update(0);composer.Compile();
  EXPECT_EQ(updates,2);EXPECT_EQ(forward,1);EXPECT_EQ(deferred,1);
}
TEST(RenderFeatures, BloomRevisionsTrackStructureAndValidateSettings)
{
  Bloom bloom;auto revision=bloom.GetConfigurationRevision();
  auto settings=bloom.GetSettings();settings.Threshold=2;settings.Intensity=0.8f;settings.SoftKnee=0;
  bloom.SetSettings(settings);EXPECT_EQ(revision,bloom.GetConfigurationRevision());
  settings.MipCount=8;bloom.SetSettings(settings);EXPECT_NE(revision,bloom.GetConfigurationRevision());
  revision=bloom.GetConfigurationRevision();settings.Enabled=false;bloom.SetSettings(settings);
  EXPECT_NE(revision,bloom.GetConfigurationRevision());
  settings.MipCount=0;EXPECT_THROW(bloom.SetSettings(settings),std::invalid_argument);
  settings.MipCount=1;settings.Intensity=-1;EXPECT_THROW(bloom.SetSettings(settings),std::invalid_argument);
}

TEST(RenderFeatures, DeferredHistoryCannotBeSatisfiedByCurrentAttachments)
{
  struct Depth final : IDeferredGeometryProvider
  {
    void BuildGraph(DeferredRenderContext& context) override
    { context.output.auxiliary[DeferredSemantics::Depth] = context.output.color; }
  };
  struct History final : IDeferredPostProcess
  {
    DeferredDependencies GetDeferredDependencies() const override
    { return {{}, {DeferredHistory::Depth}}; }
    void BuildGraphDeferred(DeferredRenderContext&) override {}
  };
  TestComposer composer;
  auto surface=composer.CreateView<Surface>();
  auto ref=composer.AddTechnique<Renderer>(&composer);
  auto& view=composer.GetTechnique<Renderer>(ref)->CreateView(Output(surface));
  view.SetViewFamily(Mono());
  auto& feature=view.SetRenderFeature<DeferredRenderFeature>();
  feature.AddProvider<Depth>();feature.AddDeferredPostProcess<History>();
  EXPECT_THROW(composer.Compile(),std::logic_error);
}
