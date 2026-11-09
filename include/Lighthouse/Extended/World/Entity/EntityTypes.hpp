#pragma once
#include "Component.hpp"

#include <entt/entity/fwd.hpp>
#include <string>
namespace Lighthouse
{
using EntityID = entt::entity;
struct EntityName { LIGHTHOUSE_COMPONENT_INSPECTOR(EntityName, {
    std::vector<char> name(component.value.begin(), component.value.end());
    name.resize(std::max<std::size_t>(name.size()+256, 1024), '\0');
    if (ImGui::InputText("Name", name.data(), name.size())) component.value = name.data();
    return false;
}) std::string value; };
};