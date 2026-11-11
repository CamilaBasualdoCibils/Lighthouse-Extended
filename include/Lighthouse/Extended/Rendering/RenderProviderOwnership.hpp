#pragma once
#include <Lighthouse/Extended/Rendering/IRenderFeature.hpp>
namespace Lighthouse
{
class RenderProviderOwnership
{
protected:
  template <class T, class Base, class... Args>
  T& Add(std::vector<std::shared_ptr<Base>>& list, Args&&... args)
  {
    auto value = std::make_shared<T>(std::forward<Args>(args)...);
    auto& result = *value;
    list.push_back(std::move(value));
    ++m_revision;
    return result;
  }
  template <class T, class Base, class... Args>
  T& Set(std::shared_ptr<Base>& slot, Args&&... args)
  {
    auto value = std::make_shared<T>(std::forward<Args>(args)...);
    auto& result = *value;
    slot = std::move(value);
    ++m_revision;
    return result;
  }
  template <class T>
  static void Hold(RenderFeatureContext& context,
                   const std::shared_ptr<T>& value)
  {
    if (value)
      context.keepAlive.push_back(value);
  }
  std::uint64_t m_revision{};
};
} // namespace Lighthouse
