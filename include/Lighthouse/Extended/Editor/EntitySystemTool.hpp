#pragma once
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Extended/World/Entity/EntityTypes.hpp>
#include <Lighthouse/Engine/Tools/ISimpleTool.hpp>
#include <functional>
namespace Lighthouse
{
class Scene;
class EntitySystemTool final : public Lighthouse::ISimpleTool
{
public:
  EntitySystemTool(Lighthouse::EngineRuntime&, Scene& world,
                   std::function<void()> onRebuild)
      : m_world(world), m_onRebuild(std::move(onRebuild))
  {
  }
  std::string_view Name() const noexcept override
  {
    return "Entities";
  }
  void Draw() override;

private:
  Scene& m_world;
  std::function<void()> m_onRebuild;
  entt::entity m_selected{entt::null};
};
} // namespace Lighthouse
#endif
