#include "../src/Editor/RendererGraphDiagram.hpp"
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/Engine/Core/EngineRuntime.hpp>
#include <Lighthouse/Engine/Rendering/ImGuiView.hpp>
#include <Lighthouse/Engine/Rendering/ImguiSimpleObject.hpp>
#include <Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>
#include <Lighthouse/Extended/Editor/RendererTool.hpp>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
using namespace Lighthouse;
namespace
{
class RendererToolTests : public testing::Test
{
  void TearDown() override
  {
    spdlog::drop("RenderGraphSimpleAllocator");
  }
};
class RecordingSimpleObject final : public ImguiSimpleObject
{
public:
  explicit RecordingSimpleObject(std::vector<int>& events) : events(events) {}
  void Draw() override
  {
    context = ImGui::GetCurrentContext();
    EXPECT_TRUE(context->WithinFrameScope);
    ImGui::GetIO().IniFilename = nullptr;
    events.push_back(1);
  }
  std::vector<int>& events;
  ImGuiContext* context{};
};

TEST_F(RendererToolTests, RendererToolUsesItsOwnGraphPassAndNodeEditorContext)
{
  EngineRuntime runtime;
  auto& toolbox = runtime.GetToolBox();
  toolbox.AddTool<RendererTool>();
  WindowManager windows;
  OpenGLBackend backend;
  std::string diagnostic;
  ASSERT_TRUE(backend.Initialize(diagnostic));
  RenderGraphComposer renderer(
      backend.CreateDevice(
          {.queues = {{.capabilities =
                           Lighthouse::RenderQueueCapability::Graphics |
                           Lighthouse::RenderQueueCapability::Compute |
                           Lighthouse::RenderQueueCapability::Transfer,
                       .minCount = 1}}}),
      &windows);
  auto handle = renderer.CreateView<ImGuiView>();
  auto* view = renderer.GetView<ImGuiView>(handle);
  view->AddObject(toolbox);
  std::vector<int> events;
  auto recorder = std::make_shared<RecordingSimpleObject>(events);
  view->AddObject(recorder);
  renderer.Compile();
  EXPECT_NE(renderer.GetRenderGraph().Dot().find("RenderGraphComposer"),
            std::string::npos);
  for (int frame = 0; frame < 3; ++frame)
    renderer.Render();
  ASSERT_NE(recorder->context, nullptr);
  ImGui::SetCurrentContext(recorder->context);
  auto* window = ImGui::FindWindowByName("RenderGraphComposer");
  ASSERT_NE(window, nullptr);
  EXPECT_NE(window->DockId, 0u);
  ImGui::SetCurrentContext(nullptr);
  // Dropping the compiled snapshot also releases the node editor safely.
  toolbox.Clear();
  renderer.Compile();
  EXPECT_NO_THROW(renderer.Render());
}

TEST_F(RendererToolTests,
       RendererInspectorShowsSceneMetadataWithoutKeepingPassesAlive)
{
  EngineRuntime runtime;
  auto& toolbox = runtime.GetToolBox();
  toolbox.AddTool<RendererTool>();
  WindowManager windows;
  OpenGLBackend backend;
  std::string diagnostic;
  ASSERT_TRUE(backend.Initialize(diagnostic));
  RenderGraphComposer renderer(
      backend.CreateDevice(
          {.queues = {{.capabilities =
                           Lighthouse::RenderQueueCapability::Graphics |
                           Lighthouse::RenderQueueCapability::Compute |
                           Lighthouse::RenderQueueCapability::Transfer,
                       .minCount = 1}}}),
      &windows);
  int sceneExecutions = 0;
  bool expanded = false;
  renderer.AddLambdaTechnique(
      [&](TechniqueBuildContext& context)
      {
        struct Data
        {
          RenderGraphResource image;
        };
        context.graph.AddPass<Data>(
            "Visible producer",
            [](RenderGraph::Builder& builder, Data& data)
            {
              data.image = builder.Create<RenderGraphBuffer>("Scene output",
                                                             {.size = 16});
            },
            [&](const Data&, FrameGraphPassResources&,
                RenderGraphExecutePassContext&) { ++sceneExecutions; });
        if (expanded)
          (void)context.graph.Import<ImportedRenderGraphBuffer>(
              "Extra external resource", {}, {});
      });
  auto ui = renderer.CreateView<ImGuiView>();
  renderer.GetView<ImGuiView>(ui)->AddObject(toolbox);
  std::vector<int> events;
  auto recorder = std::make_shared<RecordingSimpleObject>(events);
  renderer.GetView<ImGuiView>(ui)->AddObject(recorder);
  renderer.Compile();
  auto snapshot = renderer.GetRenderGraph().GetInspectionData();
  ASSERT_EQ(snapshot.passes.size(), 1u);
  ASSERT_EQ(snapshot.resources.size(), 1u);
  EXPECT_EQ(snapshot.passes[0].name, "Visible producer");
  EXPECT_EQ(snapshot.resources[0].name, "Scene output");
  const auto all = renderer.GetRenderGraph().GetInspectionData(true);
  EXPECT_GT(all.passes.size(), snapshot.passes.size());
  bool foundInspector = false;
  for (const auto& pass : all.passes)
    if (pass.name == "RenderGraphComposer")
    {
      foundInspector = true;
      EXPECT_TRUE(pass.hidden);
      // Only completion is consumed; scene output is inspected as metadata.
      ASSERT_EQ(pass.reads.size(), 1u);
      EXPECT_NE(pass.reads[0], snapshot.resources[0].handle);
    }
  EXPECT_TRUE(foundInspector);
  for (int frame = 0; frame < 3; ++frame)
    renderer.Render();
  EXPECT_EQ(sceneExecutions, 0); // The unused producer must stay culled.
  ASSERT_NE(recorder->context, nullptr);
  ImGui::SetCurrentContext(recorder->context);
  int vertices = 0;
  for (auto* viewport : ImGui::GetPlatformIO().Viewports)
    if (viewport->DrawData)
      vertices += viewport->DrawData->TotalVtxCount;
  EXPECT_GT(vertices, 0);
  ImGui::SetCurrentContext(nullptr);
  expanded = true;
  renderer.Compile();
  EXPECT_EQ(renderer.GetRenderGraph().GetInspectionData().resources.size(), 2u);
  for (int frame = 0; frame < 3; ++frame)
    EXPECT_NO_THROW(renderer.Render());
  EXPECT_EQ(sceneExecutions, 0);
}
} // namespace

// This topology triggers heap corruption in Graphviz 15.1.1 sfdp multilevel
// coarsening. The application must avoid that path without patching Graphviz.
TEST(RendererGraphLayout, SfdpHandlesBranchedGraphsAndSurvivesEngineSwitches)
{
  Detail::RendererGraphDiagram graph;
  for (unsigned i = 0; i < 24; ++i)
    graph.nodes.push_back({i + 1, "node", false, {}, 0, false});
  graph.edges = {{0, 1},   {0, 2},   {0, 3},   {0, 4},   {0, 5},   {0, 6},
                 {0, 7},   {0, 8},   {0, 9},   {0, 10},  {0, 11},  {0, 12},
                 {13, 14}, {13, 7},  {15, 16}, {15, 17}, {15, 18}, {19, 20},
                 {19, 21}, {19, 18}, {19, 2},  {19, 22}, {19, 23}};
  for (int repeat = 0; repeat < 3; ++repeat)
    for (const char* engine : {"sfdp", "dot", "neato", "fdp", "circo", "twopi",
                               "osage", "patchwork"})
    {
      SCOPED_TRACE(engine);
      graph.Layout(engine);
      ASSERT_EQ(graph.routes.size(), graph.edges.size());
      for (const auto& node : graph.nodes)
      {
        EXPECT_TRUE(std::isfinite(node.position.x));
        EXPECT_TRUE(std::isfinite(node.position.y));
      }
      for (const auto& route : graph.routes)
      {
        EXPECT_TRUE(std::isfinite(route.source.x));
        EXPECT_TRUE(std::isfinite(route.target.y));
        EXPECT_NEAR(
            std::hypot(route.sourceDirection.x, route.sourceDirection.y), 1,
            .001);
        EXPECT_NEAR(
            std::hypot(route.targetDirection.x, route.targetDirection.y), 1,
            .001);
      }
    }
}
TEST(RendererGraphLayout, RoutesUseIndividualNodeBoundariesAndLayoutDirections)
{
  Detail::RendererGraphDiagram graph;
  for (unsigned i = 0; i < 5; ++i)
    graph.nodes.push_back({i + 1, "node", false, {}, 0, false});
  graph.edges = {{0, 1}, {0, 2}, {0, 3}, {0, 4}};
  bool usesSide = false;
  for (const char* engine : {"dot", "neato", "sfdp", "circo", "twopi"})
  {
    SCOPED_TRACE(engine);
    graph.Layout(engine);
    for (std::size_t i = 0; i < graph.edges.size(); ++i)
    {
      const auto [from, to] = graph.edges[i];
      const auto& route = graph.routes[i];
      auto onBoundary = [](ImVec2 point, ImVec2 size)
      {
        EXPECT_GE(point.x, -2);
        EXPECT_LE(point.x, size.x + 2);
        EXPECT_GE(point.y, -2);
        EXPECT_LE(point.y, size.y + 2);
        EXPECT_TRUE(std::abs(point.x) < 2 || std::abs(point.x - size.x) < 2 ||
                    std::abs(point.y) < 2 || std::abs(point.y - size.y) < 2);
      };
      onBoundary(route.source, graph.nodes[from].size);
      onBoundary(route.target, graph.nodes[to].size);
      usesSide |= std::abs(route.source.x) < 2 ||
                  std::abs(route.source.x - graph.nodes[from].size.x) < 2;
    }
  }
  EXPECT_TRUE(usesSide);
}
