#pragma once
#include <Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp>
#include <algorithm>
#include <cmath>
#include <graphviz/gvc.h>
#include <graphviz/gvplugin.h>
#include <imgui.h>
#include <stdexcept>
#include <string_view>
#include <vector>
extern "C" gvplugin_library_t gvplugin_dot_layout_LTX_library;
extern "C" gvplugin_library_t gvplugin_neato_layout_LTX_library;
namespace Lighthouse::Detail
{
struct RendererGraphDiagram
{
  struct Node
  {
    std::uintptr_t id;
    std::string name;
    bool pass;
    RenderGraphResource handle;
    std::uint32_t version;
    bool imported;
    ImVec2 position{}, size{260, 110};
    std::vector<std::pair<std::size_t, bool>> pins;
  };
  struct Route
  {
    ImVec2 source, target, sourceDirection, targetDirection;
  };
  std::vector<Route> routes;
  std::vector<Node> nodes;
  std::vector<std::pair<std::size_t, std::size_t>> edges;
  std::size_t passCount{}, resourceCount{}, signature{};
  std::uintptr_t linkBase{1};
  void Layout(const char* engine)
  {
    if (nodes.empty())
      return;
    // Register the linked shared plugins without relying on a plugin config
    // file.
    struct LayoutGraph
    {
      GVC_t* context{gvContextPlugins(nullptr, false)};
      Agraph_t* graph{
          agopen(const_cast<char*>("RenderGraph"), Agdirected, nullptr)};
      bool laidOut{};
      ~LayoutGraph()
      {
        if (laidOut)
          gvFreeLayout(context, graph);
        if (graph)
          agclose(graph);
        if (context)
          gvFreeContext(context);
      }
    } layout;
    if (!layout.context || !layout.graph)
      throw std::runtime_error("Cannot create Graphviz layout context");
    gvAddLibrary(layout.context, &gvplugin_dot_layout_LTX_library);
    gvAddLibrary(layout.context, &gvplugin_neato_layout_LTX_library);
    auto attribute = [](void* object, const char* key, const std::string& value)
    {
      agsafeset(object, const_cast<char*>(key),
                const_cast<char*>(value.c_str()), const_cast<char*>(""));
    };
    // Graphviz 15.1.1 corrupts memory during sfdp multilevel coarsening
    // (upstream #2852). Its single-level solver avoids that code path.
    if (std::string_view(engine) == "sfdp" &&
        std::string_view(gvcVersion(layout.context)).starts_with("15.1.1"))
      attribute(layout.graph, "levels", "1");
    attribute(layout.graph, "rankdir", "TB");
    attribute(layout.graph, "overlap", "false");
    attribute(layout.graph, "nodesep", "0.5");
    attribute(layout.graph, "ranksep", "0.8");
    // Ask Graphviz to clip each routed edge to its node boundaries.
    attribute(layout.graph, "splines", "true");
    std::vector<Agnode_t*> vertices;
    for (const auto& node : nodes)
    {
      auto name = std::to_string(node.id);
      auto* vertex = agnode(layout.graph, name.data(), true);
      attribute(vertex, "label", "");
      attribute(vertex, "shape", "box");
      attribute(vertex, "fixedsize", "true");
      // Graphviz sizes are inches and coordinates are points (72 per inch).
      attribute(vertex, "width", std::to_string(node.size.x / 72.0f));
      attribute(vertex, "height", std::to_string(node.size.y / 72.0f));
      vertices.push_back(vertex);
    }
    std::vector<Agedge_t*> graphEdges;
    for (auto [from, to] : edges)
      graphEdges.push_back(
          agedge(layout.graph, vertices[from], vertices[to], nullptr, true));
    if (gvLayout(layout.context, layout.graph, engine) != 0)
      throw std::runtime_error(std::string("Graphviz layout failed: ") +
                               engine);
    layout.laidOut = true;
    const auto bounds = GD_bb(layout.graph);
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
      const auto center = ND_coord(vertices[i]);
      nodes[i].position = {float(center.x - bounds.LL.x) - nodes[i].size.x / 2,
                           float(bounds.UR.y - center.y) - nodes[i].size.y / 2};
    }
    auto screen = [&](pointf point)
    {
      return ImVec2(float(point.x - bounds.LL.x), float(bounds.UR.y - point.y));
    };
    auto direction = [](ImVec2 from, ImVec2 to)
    {
      const float dx = to.x - from.x, dy = to.y - from.y;
      const float length = std::hypot(dx, dy);
      return length > .001f ? ImVec2(dx / length, dy / length) : ImVec2(0, -1);
    };
    // Engines such as osage and patchwork do not produce edge splines. Use
    // their node positions to intersect the connecting line with each box.
    auto boundary = [&](std::size_t node, std::size_t other)
    {
      const auto& a = nodes[node];
      const auto& b = nodes[other];
      const ImVec2 center(a.position.x + a.size.x / 2,
                          a.position.y + a.size.y / 2);
      const ImVec2 next(b.position.x + b.size.x / 2,
                        b.position.y + b.size.y / 2);
      const auto dir = direction(center, next);
      const float scale = 1 / std::max(std::abs(dir.x) / (a.size.x / 2),
                                       std::abs(dir.y) / (a.size.y / 2));
      return ImVec2(center.x + dir.x * scale, center.y + dir.y * scale);
    };
    routes.clear();
    for (auto& node : nodes)
      node.pins.clear();
    for (std::size_t i = 0; i < edges.size(); ++i)
    {
      const auto [from, to] = edges[i];
      auto source = boundary(from, to), target = boundary(to, from);
      auto sourceDirection = direction(source, target);
      auto targetDirection = direction(target, source);
      if (const auto* spline =
              AGDATA(graphEdges[i]) ? ED_spl(graphEdges[i]) : nullptr;
          spline && spline->size)
      {
        const auto& first = spline->list[0];
        const auto& last = spline->list[spline->size - 1];
        if (first.size >= 2 && last.size >= 2)
        {
          source = screen(first.sflag ? first.sp : first.list[0]);
          target = screen(last.eflag ? last.ep : last.list[last.size - 1]);
          sourceDirection = direction(source, screen(first.list[1]));
          targetDirection = direction(target, screen(last.list[last.size - 2]));
        }
      }
      routes.push_back(
          {{source.x - nodes[from].position.x,
            source.y - nodes[from].position.y},
           {target.x - nodes[to].position.x, target.y - nodes[to].position.y},
           sourceDirection,
           targetDirection});
      nodes[from].pins.emplace_back(i, true);
      nodes[to].pins.emplace_back(i, false);
    }
  }
};
} // namespace Lighthouse::Detail
