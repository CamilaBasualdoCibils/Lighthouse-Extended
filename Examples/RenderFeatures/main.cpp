#include "Lighthouse/Engine/Rendering/ImGuiView.hpp"
#include "Lighthouse/Extended/Editor/RendererTool.hpp"
#include "Lighthouse/Extended/Editor/EntitySystemTool.hpp"
#include "Lighthouse/Extended/Resources/Resource.hpp"
#include <Lighthouse/Backends/Input/GLFW/GLFWInputBackend.hpp>
#include <Lighthouse/Engine/Engine.hpp>
#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
#include <Lighthouse/Extended/Rendering/DeferredRendering.hpp>
#include <Lighthouse/Extended/Rendering/Bloom.hpp>
#include <Lighthouse/Extended/Rendering/Renderer.hpp>
#include <Lighthouse/Extended/Rendering/SceneProvider.hpp>
#include <Lighthouse/Extended/Resources/Resources.hpp>
#include <Lighthouse/Extended/World/Entity/Light.hpp>
#include <Lighthouse/Extended/World/Entity/Transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifdef RENDER_FEATURES_EXAMPLE_OPENGL
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#endif
#ifdef RENDER_FEATURES_EXAMPLE_VULKAN
#include <Lighthouse/Backends/Render/Vulkan/VulkanBackend.hpp>
#endif

#include <Lighthouse/Extended/Rendering/EnvironmentProviders.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <imgui.h>

namespace
{
using namespace Lighthouse;

// Reserved quadrants have no rendering implementation or providers yet.
class EmptyRegionFeature final : public IRenderFeature
{
public:
  RenderFeatureResult BuildGraph(RenderFeatureContext& context) override
  {
    RenderFeatureResult result{context.destinations};
    for (auto& target : result.color)
    {
      struct Data
      {
        RenderGraphResource blank, destination;
      };
      const auto viewport = target.viewport;
      const auto& pass = context.technique.graph.AddPass<Data>(
          "Empty example region",
          [target, viewport](RenderGraph::Builder& builder, Data& data)
          {
            data.blank = builder.Write(builder.Create<RenderGraphImage>(
                "Empty region color", {.width = viewport.width,
                                       .height = viewport.height,
                                       .format = ImageFormat::RGBA8,
                                       .usage = {ImageUsage::ColorAttachment,
                                                 ImageUsage::TransferSource}}));
            data.destination = builder.Write(builder.Read(target.image));
          },
          [viewport](const Data& data, FrameGraphPassResources& resources,
                     RenderGraphExecutePassContext& execute)
          {
            const auto& blank = resources.get<RenderGraphImage>(data.blank);
            const auto& destination =
                resources.get<ImportedWindowView>(data.destination);
            auto& commands = *execute.commandList;

            commands.BeginRendering({.colorAttachment = blank.viewHandle,
                                     .clearColor = true,
                                     .clearColorValue = {0.035f, 0.04f, 0.05f},
                                     .width = viewport.width,
                                     .height = viewport.height});
            commands.EndRendering();
            commands.MemoryBarrier(
                {.srcStageMask = PipelineStage::ColorAttachmentOutput,
                 .srcAccessMask = PipelineAccess::ColorAttachmentWrite,
                 .dstStageMask = PipelineStage::Transfer,
                 .dstAccessMask = PipelineAccess::TransferRead});
            commands.BlitImage(
                blank.handle, destination.currentImage,
                {.srcSubresource = {ImageAspect::Color, 0, 0, 1},
                 .srcOffsets = {{{0, 0, 0},
                                 {viewport.width, viewport.height, 1}}},
                 .dstSubresource = {ImageAspect::Color, 0, 0, 1},
                 .dstOffsets = {{{viewport.x, viewport.y, 0},
                                 {viewport.x + viewport.width,
                                  viewport.y + viewport.height, 1}}}},
                ImageFilter::Nearest);
          });
      target.image = pass.destination;
    }
    return result;
  }
};

class RenderFeatureSettingsTool final : public ISimpleTool
{
public:
  RenderFeatureSettingsTool(EngineRuntime&,Bloom& forward,Bloom& deferred):m_forward(forward),m_deferred(deferred){}
  std::string_view Name() const noexcept override { return "Render Feature Settings"; }
  void Draw() override
  {
    if(!ImGui::BeginTabBar("RenderFeatureTabs"))return;
    if(ImGui::BeginTabItem("Forward")){DrawBloom(m_forward);ImGui::EndTabItem();}
    if(ImGui::BeginTabItem("Deferred")){DrawBloom(m_deferred);ImGui::EndTabItem();}
    if(ImGui::BeginTabItem("PathTrace"))ImGui::EndTabItem();
    ImGui::EndTabBar();
  }
private:
  static void DrawBloom(Bloom& bloom)
  {
      ImGui::SeparatorText("Mip-chain bloom");
      auto settings = bloom.GetSettings();
      bool changed = ImGui::Checkbox("Enabled", &settings.Enabled);
      ImGui::BeginDisabled(!settings.Enabled);
      int levels = static_cast<int>(settings.MipCount);
      if (ImGui::SliderInt("Mip levels", &levels, 1, 16))
      {
        settings.MipCount = static_cast<std::uint32_t>(levels);
        changed = true;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Maximum downsample levels; stops when the image reaches 1 x 1.");
      changed |= ImGui::DragFloat("Threshold", &settings.Threshold, 0.05f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      changed |= ImGui::SliderFloat("Soft knee", &settings.SoftKnee, 0.0f, 1.0f, "%.2f");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Softens highlight extraction near Threshold. Does not change blur radius or highlights far above Threshold.");
      changed |= ImGui::DragFloat("Intensity", &settings.Intensity, 0.01f, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::EndDisabled();
      if (changed)
        bloom.SetSettings(settings);
  }
  Bloom& m_forward;
  Bloom& m_deferred;
};

class RenderFeaturesExample final : public IApplication
{
public:
  ~RenderFeaturesExample() override
  {
    if (m_cameraControls)
      GetInputSystem().RemoveActionSet(m_cameraControls);
    if (m_windowControls)
      GetInputSystem().RemoveActionSet(m_windowControls);
  }

  void OnStart() override
  {
    auto& composer = GetRenderer();
    m_window = composer.CreateWindowView(
        {.title = "Lighthouse Extended - Render Features",
         .width = 1920,
         .height = 1080,
         .swapchainFormats = {Lighthouse::SwapchainFormat::HDR10,
                              Lighthouse::SwapchainFormat::ScRGB,
                              Lighthouse::SwapchainFormat::SRGB},.ImGuiHost=true});

    const auto technique = composer.AddTechnique<Renderer>(&composer);
    m_renderer = composer.GetTechnique<Renderer>(technique);
    const auto resource = Resource::LoadScene("DamagedHelmet.gltf");
    m_scene = std::make_unique<Scene>(resource.GetDefaultScene());
    m_store = std::make_shared<SceneRenderStore>(*m_scene);
    const auto addLight =
        [&](const char* name, Vec3 rotation, Vec3 color, float intensity)
    {
      auto entity = m_scene->CreateEntity(name);
      entity.Add<Transform>().SetRotEuler(glm::radians(rotation));
      auto& light = entity.Add<Light>();
      light.color = color;
      light.intensity = intensity;
    };
    addLight("Key light", {-25, -30, 0}, {1.0f, 0.92f, 0.82f}, 12.0f);
    addLight("Fill light", {10, 45, 0}, {0.65f, 0.8f, 1.0f}, 1.5f);
    addLight("Rim light", {-15, 150, 0}, {1.0f, 0.85f, 0.65f}, 9.0f);

    auto environment = std::make_shared<HDRIEnvironment>(Resource::LoadImage(
        "hayloft_2k.hdr", ImageLoadOptions{.mipPolicy = MipPolicy::Generate}));
    HDRIIndirectLightingOptions ibl;
    ibl.mode = IrradianceMode::Compute;
    GetToolBox().AddTool<EntitySystemTool>(*m_scene, [this] {});
    GetToolBox().AddTool<Lighthouse::RendererTool>();
    const auto imgui = composer.CreateView<Lighthouse::ImGuiView>(
        ImGuiViewDesc{.showImguiDemo = true, .showImplotDemo = true});
    composer.GetView<Lighthouse::ImGuiView>(imgui)->AddObject(GetToolBox());
    std::array<Bloom*,2> bloomSettings{};
    for (std::size_t i = 0; i < m_views.size(); ++i)
    {
      auto& view = m_renderer->CreateView(
          std::make_shared<WindowRenderOutput>(m_window));
      m_views[i] = &view;

      if (i == 0)
      {
        auto& forward = view.SetRenderFeature<ForwardRenderFeature>();
        forward.AddProvider<SceneProvider>(m_store);
        auto& bloom = forward.AddForwardPostProcess<Bloom>(
            BloomSettings{.Threshold=1.0f,.SoftKnee=0.5f,.Intensity=0.35f,.MipCount=6});
        bloomSettings[0]=&bloom;
        forward.SetLighting<DefaultForwardLightingProvider>(m_store);
        forward.SetEnvironmentProvider<EnvironmentProvider>(
            std::make_shared<HDRISkyProvider>(environment));
        forward.SetIndirectLightingProvider<HDRIIndirectLightingProvider>(
            environment, ibl);
      }
      else if(i==1)
      {
        auto& deferred=view.SetRenderFeature<DeferredRenderFeature>();
        deferred.AddProvider<SceneDeferredRenderProvider>(m_store);
        deferred.SetLighting<DefaultDeferredLightingProvider>(m_store);
        deferred.SetEnvironmentProvider<EnvironmentProvider>(std::make_shared<HDRISkyProvider>(environment));
        deferred.SetIndirectLightingProvider<HDRIIndirectLightingProvider>(environment,ibl);
        bloomSettings[1]=&deferred.AddDeferredPostProcess<Bloom>(BloomSettings{.Threshold=1.0f,.SoftKnee=0.5f,.Intensity=0.35f,.MipCount=6});
      }
      else
      {
        view.SetRenderFeature<EmptyRegionFeature>();
      }
    }

    GetToolBox().AddTool<RenderFeatureSettingsTool>(*bloomSettings[0],*bloomSettings[1]);
    UpdateLayout();
    SetupInput();
    m_renderer->Update(0.0f);
    composer.Compile();
    std::cout << composer.GetRenderGraph().Dot() << std::endl;
  }

  void OnUpdate(float deltaTime) override
  {
    const auto size = GetRenderer()
                          .GetView<WindowView>(m_window)
                          ->GetWindow()
                          ->GetFramebufferSize();
    if (size.width < 2 || size.height < 2)
      return;
    if (size.width != m_size.x || size.height != m_size.y)
      UpdateLayout();
    UpdateCamera(deltaTime);
    m_renderer->Update(deltaTime);
    if (m_renderer->NeedsRebuild())
      GetRenderer().Compile();
    GetRenderer().Render();
  }

private:
  Vec3 CameraForward() const
  {
    return {std::sin(m_yaw) * std::cos(m_pitch), std::sin(m_pitch),
            -std::cos(m_yaw) * std::cos(m_pitch)};
  }

  void SetupInput()
  {
    auto& input = GetInputSystem();
    auto& backend = input.GetBackend<GLFWInputBackend>();
    const auto window =
        GetRenderer().GetView<WindowView>(m_window)->GetWindow()->Handle();
    const auto keyboards = backend.EnumerateKeyboards();
    const auto mice = backend.EnumerateMice();
    if (keyboards.empty() || mice.empty())
      throw std::runtime_error(
          "RenderFeatures camera requires a keyboard and mouse");
    auto keyboard = backend.OpenDevice(keyboards.front().id, window);
    auto mouse = backend.OpenDevice(mice.front().id, window);
    m_cameraControls = input.CreateActionSet("RenderFeatures Camera");
    m_move = m_cameraControls->AddAction("Move", InputActionType::Axis2D);
    m_move->BindComposite2D(keyboard, Key::A, Key::D, Key::S, Key::W);
    m_elevate = m_cameraControls->AddAction("Elevate", InputActionType::Axis1D);
    m_elevate->Bind(keyboard, InputControl::Button(Key::Space), {1, 0});
    m_elevate->Bind(keyboard, InputControl::Button(Key::LeftShift), {-1, 0});
    m_elevate->Bind(keyboard, InputControl::Button(Key::RightShift), {-1, 0});
    m_look = m_cameraControls->AddAction("Look", InputActionType::Delta2D);
    m_look->Bind(mouse, InputControl::Delta(MouseAxis::X), {1, 0});
    m_look->Bind(mouse, InputControl::Delta(MouseAxis::Y), {0, 1});
    m_cameraControls->SetEnabled(true);

    m_windowControls = input.CreateActionSet("RenderFeatures Window");
    auto escape =
        m_windowControls->AddAction("Toggle cursor", InputActionType::Button);
    escape->Bind(keyboard, InputControl::Button(Key::Escape));
    escape->OnPressed(
        [this, window]
        {
          m_cursorCaptured = !m_cursorCaptured;
          m_skipMouseDelta = true;
          GetWindowManager().SetCursorMode(window, m_cursorCaptured
                                                       ? CursorMode::Captured
                                                       : CursorMode::Normal);
        });
    m_windowControls->SetEnabled(true);
    GetWindowManager().SetCursorMode(window, CursorMode::Captured);
    for(std::size_t i=0;i<2;++i)
    m_views[i]->SetViewSource(
        [this,i]
        {
          RenderViewData camera;
          camera.id = i+1;
          camera.resolution =
              m_views[i]->GetViewFamily().views.front().resolution;
          camera.cameraPosition = m_cameraPosition;
          camera.view =
              glm::lookAtRH(m_cameraPosition,
                            m_cameraPosition + CameraForward(), Vec3(0, 1, 0));
          camera.projection = glm::perspectiveRH_ZO(
              glm::radians(45.0f),
              float(camera.resolution.x) / float(camera.resolution.y), 0.1f,
              100.0f);
          return RenderViewFamily{{camera}};
        });
  }

  void UpdateCamera(float deltaTime)
  {
    const auto* window =
        GetRenderer().GetView<WindowView>(m_window)->GetWindow();
    const bool active = m_cursorCaptured && window->IsFocused();
    m_cameraControls->SetEnabled(active);
    if (!active)
    {
      m_skipMouseDelta = true;
      return;
    }
    if (!m_skipMouseDelta)
    {
      const auto look = m_look->GetAxis2D();
      constexpr float sensitivity = 0.002f;
      m_yaw =
          std::remainder(m_yaw + look.x * sensitivity, glm::two_pi<float>());
      m_pitch = std::clamp(m_pitch - look.y * sensitivity, -glm::radians(89.0f),
                           glm::radians(89.0f));
    }
    m_skipMouseDelta = false;
    const auto move = m_move->GetAxis2D();
    const auto forward = CameraForward();
    const auto right = glm::normalize(glm::cross(forward, Vec3(0, 1, 0)));
    auto movement =
        forward * move.y + right * move.x + Vec3(0, m_elevate->GetAxis1D(), 0);
    const auto length = glm::length(movement);
    if (length > 1)
      movement /= length;
    m_cameraPosition += movement * (3.0f * deltaTime);
  }

  void UpdateLayout()
  {
    const auto size = GetRenderer()
                          .GetView<WindowView>(m_window)
                          ->GetWindow()
                          ->GetFramebufferSize();
    m_size = {size.width, size.height};
    const auto halfWidth = size.width / 2;
    const auto halfHeight = size.height / 2;
    // Top left, top right, bottom left, bottom right. Remainders cover odd
    // sizes.
    std::array<RenderViewport, 4> regions{{
        {0, 0, halfWidth, halfHeight},
        {halfWidth, 0, size.width - halfWidth, halfHeight},
        {0, halfHeight, halfWidth, size.height - halfHeight},
        {halfWidth, halfHeight, size.width - halfWidth,
         size.height - halfHeight},
    }};
#ifdef RENDER_FEATURES_EXAMPLE_OPENGL
    // OpenGL target pixel coordinates start at the lower-left corner.
    if (dynamic_cast<OpenGLBackend*>(GetGraphicsBackend()))
      for (auto& region : regions)
        region.y = size.height - region.y - region.height;
#endif
    for (std::size_t i = 0; i < regions.size(); ++i)
    {
      m_views[i]->SetOutput(
          std::make_shared<WindowRenderOutput>(m_window, regions[i]));
      RenderViewData camera;
      camera.id = i + 1;
      camera.resolution = {regions[i].width, regions[i].height};
      camera.cameraPosition = {0, 0, 3.5f};
      camera.view = glm::lookAtRH(camera.cameraPosition, Vec3(0, -0.15f, 0),
                                  Vec3(0, 1, 0));
      camera.projection = glm::perspectiveRH_ZO(glm::radians(45.0f),
                                                float(camera.resolution.x) /
                                                    float(camera.resolution.y),
                                                0.1f, 100.0f);
      m_views[i]->SetViewFamily({{camera}});
    }
  }

  RenderViewHandle m_window;
  Renderer* m_renderer{};
  std::array<RenderFeatureView*, 4> m_views{};
  UVec2 m_size{};
  std::unique_ptr<Scene> m_scene;
  std::shared_ptr<SceneRenderStore> m_store;
  std::shared_ptr<InputActionSet> m_cameraControls, m_windowControls;
  std::shared_ptr<InputAction> m_move, m_elevate, m_look;
  Vec3 m_cameraPosition{0, 0, 3.5f};
  float m_yaw{}, m_pitch{std::atan2(-0.15f, 3.5f)};
  bool m_cursorCaptured{true}, m_skipMouseDelta{true};
};
} // namespace


int main()
{
  Lighthouse::Config config{
      .name = "Lighthouse Extended Render Features Example",
      .graphics = {.selection = Lighthouse::BackendSelection::FirstAvailable},
  };
  config.AddInputBackend<Lighthouse::GLFWInputBackend>();
#ifdef RENDER_FEATURES_EXAMPLE_VULKAN
  config.AddRenderBackend<Lighthouse::VulkanBackend>();
#endif
#ifdef RENDER_FEATURES_EXAMPLE_OPENGL
  config.AddRenderBackend<Lighthouse::OpenGLBackend>();
#endif

  return Lighthouse::Run<RenderFeaturesExample>(config);
}
