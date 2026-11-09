#include <Lighthouse/Extended/World/Scene.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

struct ValueComponent {
    explicit ValueComponent(int value) : value(value) {}
    int value;
};

class MockComponent {
public:
    MOCK_METHOD(int, Invoke, ());
};

struct MockComponentHolder {
    explicit MockComponentHolder(std::shared_ptr<MockComponent> component)
        : component(std::move(component)) {}

    std::shared_ptr<MockComponent> component;
};

TEST(EntityTests, AddsComponentsAndReturnsStoredReference) {
    Lighthouse::Scene scene{"Main"};
    auto entity = scene.CreateEntity("Player");

    auto& component = entity.Add<ValueComponent>(42);

    EXPECT_EQ(component.value, 42);
    EXPECT_TRUE(entity.IsValid());
    EXPECT_EQ(entity.Name(), "Player");
}

TEST(EntityTests, SupportsGoogleMockComponents) {
    Lighthouse::Scene scene{"Main"};
    auto entity = scene.CreateEntity("Mocked");
    auto mock = std::make_shared<MockComponent>();
    auto& holder = entity.Add<MockComponentHolder>(mock);

    EXPECT_CALL(*holder.component, Invoke()).WillOnce(testing::Return(7));
    EXPECT_EQ(holder.component->Invoke(), 7);
}

TEST(SceneTests, CreatesEntitiesWithNames) {
    Lighthouse::Scene scene{"Main"};
    auto first = scene.CreateEntity("First");
    auto second = scene.CreateEntity("Second");

    EXPECT_EQ(scene.Name(), "Main");
    EXPECT_TRUE(first.IsValid());
    EXPECT_TRUE(second.IsValid());
    EXPECT_EQ(first.Name(), "First");
    EXPECT_EQ(second.Name(), "Second");
}

} // namespace
