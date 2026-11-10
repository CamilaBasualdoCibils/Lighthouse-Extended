#pragma once
#include "Component.hpp"
#include <Lighthouse/Extended/Resources/Model.hpp>
#include <Lighthouse/Extended/Resources/Material.hpp>
namespace Lighthouse
{
struct MeshRender
{
  LIGHTHOUSE_COMPONENT_INSPECTOR(MeshRender, {
    ImGui::TextUnformatted(component.mesh ? "Mesh assigned" : "No mesh");
    ImGui::TextUnformatted(component.material ? "Material assigned" : "No material");
    return false;
})
  ModelHandle mesh{};
  MaterialHandle material{};
};
} // namespace Lighthouse
