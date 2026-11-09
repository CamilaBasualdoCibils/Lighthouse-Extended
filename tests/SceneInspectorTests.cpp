#include <Lighthouse/Extended/World/Entity/Component.hpp>
#include <Lighthouse/Extended/World/Scene.hpp>
#include <gtest/gtest.h>

#ifdef LIGHTHOUSE_IMGUI
namespace {
struct InspectedComponent {
    int calls{};
    static bool InspectComponent(InspectedComponent& component) {
        ++component.calls;
        ImGui::Button("Shared label");
        return false;
    }
};
struct OtherInspectedComponent {
    int calls{};
    static bool InspectComponent(OtherInspectedComponent& component) {
        ++component.calls;
        ImGui::Button("Shared label");
        return false;
    }
};
struct UninspectedComponent { int value{42}; };
inline const bool inspectedRegistered = Lighthouse::RegisterComponentInspector<InspectedComponent>("InspectedComponent");
inline const bool otherRegistered = Lighthouse::RegisterComponentInspector<OtherInspectedComponent>("OtherInspectedComponent");

TEST(SceneInspectorTests, InvokesInspectorsOnStoredComponentsAndSkipsOthers) {
    auto* context = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    Lighthouse::Scene scene("Inspector test");
    auto first = scene.CreateEntity("First");
    auto second = scene.CreateEntity("Second");
    auto empty = scene.CreateEntity("No components");
    auto& firstComponent = first.Add<InspectedComponent>();
    auto& secondComponent = second.Add<InspectedComponent>();
    auto& otherComponent = first.Add<OtherInspectedComponent>();
    auto& skipped = first.Add<UninspectedComponent>();

    // Also cover a reflected type that has no Inspector function.
    entt::meta_factory<UninspectedComponent>();

    // Open every entity node without requiring a window system or renderer.
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(800, 600));
    ImGui::Begin("Scene inspector");
    for (auto entity : {first.ID(), second.ID(), empty.ID()}) {
        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
        ImGui::GetStateStorage()->SetBool(ImGui::GetID("##Entity"), true);
        ImGui::PopID();
    }
    for (auto entity : {first.ID(), second.ID(), empty.ID()}) {
      for (auto [id, storage] : scene.Registry().storage()) {
        if (!storage.contains(entity)) continue;
        auto type = entt::resolve(storage.info());
        if (auto inspect = type.func(Lighthouse::InspectEntityID))
          inspect.invoke({}, entt::forward_as_meta(scene.Registry()), entity);
      }
    }
    ImGui::End();
    ImGui::Render();

    EXPECT_EQ(firstComponent.calls, 1);
    EXPECT_EQ(secondComponent.calls, 1);
    EXPECT_EQ(otherComponent.calls, 1);
    EXPECT_EQ(skipped.value, 42);
    ImGui::DestroyContext(context);
}
} // namespace
#endif
