#include "Lighthouse/Extended/Resources/Resource.hpp"
#include <Lighthouse/Engine/Engine.hpp>
#include <spdlog/spdlog.h>

#ifdef GLTF_EXAMPLE_OPENGL
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#endif
#ifdef GLTF_EXAMPLE_VULKAN
#include <Lighthouse/Backends/Render/Vulkan/VulkanBackend.hpp>
#endif

#include <filesystem>
#include <iostream>

namespace
{
std::filesystem::path gltfPath;

class GLTFLoadExample final : public Lighthouse::IApplication
{
public:
    void OnStart() override
    {
        GetRenderer().CreateWindowView({
            .title = "Lighthouse Extended - glTF Load",
            .width = 1280,
            .height = 720,
        });
        spdlog::info("parsing glTF asset from path: {}", gltfPath.string());
        const auto m_asset = Lighthouse::Resource::LoadScene(gltfPath);
        const auto source = std::get<Lighthouse::Resource::GLTFScene>(m_asset.GetSource());
        spdlog::info("Loaded glTF: {} meshes, {} scenes",
                     source.asset->meshes.size(), source.asset->scenes.size());
    }

    void OnUpdate(float) override {}

private:
    
};
} // namespace

int main(int argc, char** argv)
{
    if (argc > 2)
    {
        std::cerr << "Usage: " << argv[0] << " <file.gltf|file.glb>\n";
        return 1;
    }
    gltfPath = argc == 2 ? std::filesystem::path(argv[1])
                         : std::filesystem::path(GLTF_EXAMPLE_DEFAULT_MODEL);

    Lighthouse::Config config{
        .name = "Lighthouse Extended glTF Load Example",
        .graphics = {.selection = Lighthouse::BackendSelection::FirstAvailable},
    };
#ifdef GLTF_EXAMPLE_OPENGL
    config.AddRenderBackend<Lighthouse::OpenGLBackend>();
#endif
#ifdef GLTF_EXAMPLE_VULKAN
    config.AddRenderBackend<Lighthouse::VulkanBackend>();
#endif
    return Lighthouse::Run<GLTFLoadExample>(config);
}
