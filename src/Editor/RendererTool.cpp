#ifdef LIGHTHOUSE_IMGUI
#include "RendererGraphDiagram.hpp"
#include <Lighthouse/Extended/Editor/RendererTool.hpp>
#include <algorithm>
#include <cmath>
#include <imgui-node-editor/imgui_node_editor.h>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace Lighthouse
{
namespace
{
constexpr const char* layoutEngines[] = {
    "dot", "neato", "fdp", "sfdp", "circo", "twopi", "osage", "patchwork"};
}

struct RendererTool::Diagram : Detail::RendererGraphDiagram
{
};

std::shared_ptr<RendererTool::Diagram>
RendererTool::MakeDiagram(RenderGraph::InspectionData data)
{
  auto diagram = std::make_shared<Diagram>();
  diagram->passCount = data.passes.size();
  diagram->resourceCount = data.resources.size();
  std::unordered_map<RenderGraphResource, std::size_t> resources;
  for (const auto& resource : data.resources)
  {
    resources.emplace(resource.handle, diagram->nodes.size());
    diagram->nodes.push_back({(std::uintptr_t(resource.handle) + 1) * 8,
                              resource.name, false, resource.handle,
                              resource.version, resource.imported});
  }
  std::set<std::pair<std::size_t, std::size_t>> edges;
  for (const auto& pass : data.passes)
  {
    const auto index = diagram->nodes.size();
    diagram->nodes.push_back({(std::uintptr_t(pass.id) * 2 + 1) * 4, pass.name,
                              true, kInvalidGraphResource, 0, false});
    for (auto handle : pass.reads)
      if (const auto found = resources.find(handle); found != resources.end())
        edges.emplace(found->second, index);
    for (const auto* outputs : {&pass.creates, &pass.writes})
      for (auto handle : *outputs)
        if (const auto found = resources.find(handle); found != resources.end())
          edges.emplace(index, found->second);
  }
  diagram->edges.assign(edges.begin(), edges.end());
  auto hash = [&](std::size_t value)
  {
    diagram->signature ^= value + std::size_t(0x9e3779b9) +
                          (diagram->signature << 6) + (diagram->signature >> 2);
  };
  for (const auto& node : diagram->nodes)
  {
    hash(node.id);
    hash(std::hash<std::string>{}(node.name));
    hash(node.version);
    hash(node.imported);
    diagram->linkBase = std::max(diagram->linkBase, node.id + 4);
  }
  for (auto [from, to] : diagram->edges)
  {
    hash(from);
    hash(to);
  }
  return diagram;
}
RendererTool::~RendererTool()
{
  if (m_editor)
    ax::NodeEditor::DestroyEditor(m_editor);
}
RenderGraphResource RendererTool::BuildGraph(ImguiBuildContext& context,
                                             RenderGraphResource completionFlag)
{
  struct Data
  {
    RenderGraphResource completion;
    std::shared_ptr<Diagram> diagram;
  };
  auto diagram = MakeDiagram(context.graph.GetInspectionData());
  return context
      .AddPass<Data>(
          Name(),
          [completionFlag, diagram](RenderGraph::Builder& builder, Data& data)
          {
            data.diagram = diagram;
            data.completion = builder.Write(completionFlag);
          },
          [this, dockspace = context.dockspace](const Data& data,
                                                FrameGraphPassResources&,
                                                RenderGraphExecutePassContext&)
          {
            if (dockspace)
              ImGui::SetNextWindowDockID(dockspace, ImGuiCond_FirstUseEver);
            const bool visible = ImGui::Begin("RenderGraphComposer");
            try
            {
              if (visible)
                Draw(data.diagram);
            }
            catch (...)
            {
              ImGui::End();
              throw;
            }
            ImGui::End();
          })
      .completion;
}
void RendererTool::Draw(const std::shared_ptr<Diagram>& candidate)
{
  namespace ed = ax::NodeEditor;
  auto* previous = ed::GetCurrentEditor();
  const bool changed = !m_hasSignature || m_signature != candidate->signature;
  if (changed && m_editor)
  {
    if (previous == m_editor)
      previous = nullptr;
    ed::DestroyEditor(m_editor);
    m_editor = nullptr;
  }
  if (!m_editor)
  {
    ed::Config config;
    config.SettingsFile = nullptr;
    m_editor = ed::CreateEditor(&config);
  }
  struct Restore
  {
    ed::EditorContext* previous;
    ~Restore()
    {
      ed::SetCurrentEditor(previous);
    }
  } restore{previous};
  ed::SetCurrentEditor(m_editor);
  ed::EnableShortcuts(false);
  if (changed)
  {
    m_diagram = candidate;
    m_diagram->Layout(layoutEngines[m_layoutEngine]);
    m_signature = candidate->signature;
    m_hasSignature = true;
    m_fitFrames = 2;
  }
  const auto& diagram = m_diagram;
  ImGui::SetNextItemWidth(180);
  if (ImGui::Combo("Layout engine", &m_layoutEngine, layoutEngines,
                   IM_ARRAYSIZE(layoutEngines)))
  {
    diagram->Layout(layoutEngines[m_layoutEngine]);
    m_fitFrames = 2;
  }
  ImGui::Text("%zu passes / %zu resource versions (read only)",
              diagram->passCount, diagram->resourceCount);
  if (ImGui::Button("Fit graph"))
    m_fitFrames = 2;
  ImGui::SameLine();
  ImGui::TextDisabled("Pan / zoom / select");
  if (diagram->nodes.empty())
    ImGui::TextUnformatted("No rendering passes or resources.");
  ed::PushStyleVar(ed::StyleVar_NodeRounding, 16.0f);

  ed::Begin("RendererGraph");
  for (const auto& node : diagram->nodes)
  {
    ed::SetNodePosition(ed::NodeId(node.id), node.position);
    const ImVec4 nodeColor = node.pass ? ImVec4(1, .65f, 0, 1)
                                      : node.imported ? ImVec4(.55f, .55f, .55f, 1)
                                                      : ImVec4(.53f, .81f, .9f, 1);
    ed::PushStyleColor(ed::StyleColor_NodeBg, nodeColor);
    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0, 0, 0, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
    ed::BeginNode(ed::NodeId(node.id));
    const auto top = ImGui::GetCursorScreenPos();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240);
    ImGui::TextUnformatted(node.name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    if (!node.pass)
      ImGui::Text("v%u%s", node.version, node.imported ? " (imported)" : "");
    else
      ImGui::Dummy(ImVec2(240, ImGui::GetTextLineHeight()));
    ImGui::Dummy(ImVec2(240, 1));
    const auto padding = ed::GetStyle().NodePadding;
    const ImVec2 origin(top.x - padding.x, top.y - padding.y);
    for (auto [edge, output] : node.pins)
    {
      const auto& route = diagram->routes[edge];
      const auto offset = output ? route.source : route.target;
      const auto tangent =
          output ? route.sourceDirection : route.targetDirection;
      const ImVec2 pivot(origin.x + offset.x, origin.y + offset.y);
      ed::PushStyleVar(output ? ed::StyleVar_SourceDirection
                              : ed::StyleVar_TargetDirection,
                       tangent);
      ed::PushStyleVar(ed::StyleVar_SnapLinkToPinDir, 1.0f);
      ed::PushStyleVar(ed::StyleVar_PinArrowSize, output ? 0.0f : 14.0f);
      ed::PushStyleVar(ed::StyleVar_PinArrowWidth, output ? 0.0f : 12.0f);
      ed::BeginPin(ed::PinId(diagram->linkBase + edge * 3 + (output ? 1 : 2)),
                   output ? ed::PinKind::Output : ed::PinKind::Input);
      ed::PinRect(pivot, pivot);
      ed::PinPivotRect(pivot, pivot);
      ed::EndPin();
      ed::PopStyleVar(4);
    }
    ed::EndNode();
    ImGui::PopStyleColor();
    ed::PopStyleColor(2);
  }
  for (std::size_t i = 0; i < diagram->edges.size(); ++i)
  {
    const auto [from, to] = diagram->edges[i];
    ed::Link(ed::LinkId(diagram->linkBase + i * 3),
             ed::PinId(diagram->linkBase + i * 3 + 1),
             ed::PinId(diagram->linkBase + i * 3 + 2),
             diagram->nodes[from].pass ? ImVec4(1, .25f, 0, 1)
                                       : ImVec4(.55f, .8f, .12f, 1),
             2.0f);
  }
  // No create/delete/shortcut handlers: the source graph is never editable.
  ed::End();
  ed::PopStyleVar();
  bool resized = false;
  for (auto& node : diagram->nodes)
  {
    const auto size = ed::GetNodeSize(ed::NodeId(node.id));
    if (size.x > 0 && size.y > 0 &&
        (std::abs(size.x - node.size.x) > .5f ||
         std::abs(size.y - node.size.y) > .5f))
    {
      node.size = size;
      resized = true;
    }
  }
  if (resized)
    diagram->Layout(layoutEngines[m_layoutEngine]);
  // Keep automatic positions fixed while retaining canvas navigation/selection.
  for (const auto& node : diagram->nodes)
    ed::SetNodePosition(ed::NodeId(node.id), node.position);
  if (m_fitFrames > 0 && !diagram->nodes.empty())
  {
    ed::NavigateToContent(0);
    --m_fitFrames;
  }
}
} // namespace Lighthouse
#endif
