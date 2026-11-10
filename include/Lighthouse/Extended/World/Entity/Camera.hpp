#pragma once
#include "Component.hpp"

#include <Lighthouse/Extended/Math/Types.hpp>
#include <Lighthouse/Engine/Assets/Assets.hpp>
namespace Lighthouse {

class Camera {
public:
    Camera() = default;
    explicit Camera(CameraAsset asset) : source(std::move(asset)) {}
    std::optional<CameraAsset> source;
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