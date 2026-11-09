#pragma once
#include <Lighthouse/Extended/World/Entity/Entity.hpp>
#include <memory>
#include <string>
namespace Lighthouse {
class Scene {
public:
 explicit Scene(std::string name);
 Entity CreateEntity(std::string name);
 const std::string& Name() const;
 entt::registry& Registry() { return *entity_registry; }
 const entt::registry& Registry() const { return *entity_registry; }
private:
 std::shared_ptr<entt::registry> entity_registry;
 std::string m_name;
};
}
