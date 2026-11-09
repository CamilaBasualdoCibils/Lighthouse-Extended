#include <Lighthouse/Extended/World/Scene.hpp>
namespace Lighthouse {
Scene::Scene(std::string name) : entity_registry(std::make_shared<entt::registry>()), m_name(std::move(name)) {}
Entity Scene::CreateEntity(std::string name) {
    const auto entity = entity_registry->create();
    return Entity{entity_registry, entity, std::move(name)};
}

const std::string& Scene::Name() const {
    return m_name;
}

}
