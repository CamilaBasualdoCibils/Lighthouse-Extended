#pragma once
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Tools/ITool.hpp>
namespace ax::NodeEditor
{
struct EditorContext;
}
namespace Lighthouse
{
class RendererTool final : public ITool
{
public:
  explicit RendererTool(EngineRuntime&) {}
  ~RendererTool() override;
  RendererTool(const RendererTool&) = delete;
  RendererTool& operator=(const RendererTool&) = delete;
  std::string_view Name() const noexcept override
  {
    return "RenderGraphComposer";
  }
  RenderGraphResource BuildGraph(ImguiBuildContext& context,
                                 RenderGraphResource completionFlag) override;

private:
  struct Diagram;
  static std::shared_ptr<Diagram> MakeDiagram(RenderGraph::InspectionData data);
  void Draw(const std::shared_ptr<Diagram>& diagram);
  std::size_t m_signature{};
  bool m_hasSignature{};
  std::shared_ptr<Diagram> m_diagram;
  int m_layoutEngine{};
  int m_fitFrames{};
  ax::NodeEditor::EditorContext* m_editor{};
};
} // namespace Lighthouse
#endif
