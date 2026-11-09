#pragma once
#include "Component.hpp"

#include <Lighthouse/Extended/Math/Types.hpp>
namespace Lighthouse {

class Camera {
    LIGHTHOUSE_COMPONENT_INSPECTOR(Camera, {
    ImGui::DragFloat("Horizontal FOV", &component.FOVx, .1f, 1, 179);
    ImGui::DragFloat3("Position offset", &component.positionOffset.x, .1f);
    return false;
})
private:
    float FOVx = 90.0f;
    Vec3 positionOffset{};
    
    
};

} // namespace Lighthouse