#include <Lighthouse/Extended/Resources/Resource.hpp>
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>

using namespace Lighthouse;
namespace
{
class ResourceSceneTest : public testing::Test
{
protected:
  std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("lighthouse-model-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  void SetUp() override
  {
    std::filesystem::create_directory(directory);
    std::ofstream(directory / "model.gltf") << R"({
      "asset":{"version":"2.0"}, "scene":1,
      "nodes":[{"name":"Root"}],
      "scenes":[{"name":"First","nodes":[0]}, {"name":"Second","nodes":[0]}]
    })";
    std::ofstream(directory / "bad.gltf") << "invalid gltf";
  }
  void TearDown() override
  {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
  }
};
} // namespace

TEST_F(ResourceSceneTest, OwnsLoadedAssetAndPreservesScenes)
{
  const auto path = directory / "model.gltf";
  auto model = Resource::LoadScene(path);
  auto alias = model;
  auto source = std::get<Resource::GLTFScene>(model.GetSource());
  ASSERT_NE(source.asset, nullptr);
  EXPECT_EQ(source.path, path);
  ASSERT_EQ(source.asset->scenes.size(), 2u);
  EXPECT_EQ(source.asset->scenes[1].name, "Second");
  EXPECT_EQ(source.asset->defaultScene, 1u);
  EXPECT_EQ(model.GetRevision(), 1u);
  EXPECT_EQ(model, alias);
  EXPECT_NE(model.GetID(), Resource::LoadScene(path).GetID());
  const auto id = model.GetID();
  alias.UpdateGLTF(path);
  EXPECT_EQ(model.GetID(), id);
  EXPECT_EQ(model.GetRevision(), 2u);
  EXPECT_NE(source.asset,
            std::get<Resource::GLTFScene>(model.GetSource()).asset);
  std::filesystem::remove(path);
  EXPECT_EQ(source.asset->nodes[0].name, "Root");
}

TEST_F(ResourceSceneTest, LoadErrorsLeaveExistingResourceUnchanged)
{
  auto model = Resource::LoadScene(directory / "model.gltf");
  auto source = std::get<Resource::GLTFScene>(model.GetSource());
  EXPECT_THROW(Resource::LoadScene({}), std::invalid_argument);
  EXPECT_THROW(Resource::LoadScene(directory / "missing.gltf"),
               std::runtime_error);
  EXPECT_THROW(Resource::LoadScene(directory / "bad.gltf"), std::runtime_error);
  EXPECT_THROW(model.UpdateGLTF(directory / "bad.gltf"), std::runtime_error);
  EXPECT_EQ(model.GetRevision(), 1u);
  EXPECT_EQ(source.asset,
            std::get<Resource::GLTFScene>(model.GetSource()).asset);
}

#include <Lighthouse/Extended/World/Entity/Camera.hpp>
#include <Lighthouse/Extended/World/Entity/MeshRender.hpp>
#include <Lighthouse/Extended/World/Entity/Transform.hpp>

TEST_F(ResourceSceneTest, CreatesIndependentScenesAndSharedPartHandles)
{
  std::ofstream(directory / "parts.gltf") << R"({
    "asset":{"version":"2.0"}, "scene":1,
    "buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"}],
    "bufferViews":[{"buffer":0,"byteLength":36}],
    "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],
    "images":[{"uri":"not-decoded.png"}],
    "samplers":[{"magFilter":9728,"minFilter":9986,"wrapS":33071,"wrapT":33648}],
    "textures":[{"source":0,"sampler":0}],
    "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
    "meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0},{"attributes":{"POSITION":0},"material":0}]}],
    "cameras":[{"type":"perspective","perspective":{"yfov":1.0,"znear":0.1}}],
    "nodes":[{"name":"Root","translation":[1,0,0],"children":[1]},
             {"name":"Child","mesh":0,"camera":0,"translation":[0,2,0]}],
    "scenes":[{"name":"First","nodes":[0]},{"name":"Second","nodes":[0]}]
  })";
  auto resource = Resource::LoadScene(directory / "parts.gltf");
  auto scene = resource.GetDefaultScene();
  auto other = resource.GetScene(1);
  EXPECT_EQ(scene.Name(), "Second");
  EXPECT_NE(&scene.Registry(), &other.Registry());
  EXPECT_EQ(resource.GetScenes().size(), 2u);
  EXPECT_THROW(resource.GetScene(2), std::out_of_range);
  auto models = resource.GetModels();
  auto materials = resource.GetMaterials();
  ASSERT_EQ(models.size(), 2u);
  ASSERT_EQ(materials.size(), 1u);
  EXPECT_EQ(models[0], resource.GetModels()[0]);
  const auto gltf = std::get<Resource::GLTFScene>(resource.GetSource()).asset;
  EXPECT_EQ(std::get<Resource::GLTFModel>(models[0].GetSource()).asset, gltf);
  EXPECT_EQ(std::get<Resource::GLTFMaterial>(materials[0].GetSource()).asset,
            gltf);
  ASSERT_EQ(resource.GetTextures().size(), 1u);
  ASSERT_EQ(resource.GetImages().size(), 1u);
  auto texture = resource.GetTextures()[0];
  EXPECT_EQ(materials[0].GetTexture("baseColor"), texture);
  EXPECT_EQ(texture.GetSampler().magnification, SamplerFilter::Nearest);
  EXPECT_EQ(texture.GetSampler().minification, SamplerFilter::Nearest);
  EXPECT_EQ(texture.GetSampler().mipmap, SamplerMipmapFilter::Linear);
  EXPECT_EQ(texture.GetSampler().addressU, SamplerAddressMode::ClampToEdge);
  EXPECT_EQ(texture.GetSampler().addressV, SamplerAddressMode::MirroredRepeat);
  EXPECT_EQ(std::get<Resource::GLTFImage>(texture.GetImage().GetSource()).asset,
            gltf);
  int transforms = 0, meshes = 0, cameras = 0;
  for (auto entity : scene.Registry().view<Transform>())
  {
    ++transforms;
    const auto& transform = scene.Registry().get<Transform>(entity);
    if (scene.Registry().all_of<Camera>(entity))
    {
      ++cameras;
      EXPECT_TRUE(transform.HasParent());
      EXPECT_EQ(transform.GetPos().y, 2.0f);
      EXPECT_EQ(
          scene.Registry().get<Transform>(*transform.GetParent()).GetPos().x,
          1.0f);
      EXPECT_TRUE(scene.Registry().get<Camera>(entity).source.has_value());
    }
    if (scene.Registry().all_of<MeshRender>(entity))
    {
      ++meshes;
      const auto& render = scene.Registry().get<MeshRender>(entity);
      EXPECT_TRUE(bool(render.mesh));
      EXPECT_EQ(render.material, materials[0]);
    }
  }
  EXPECT_EQ(transforms, 3);
  EXPECT_EQ(meshes, 2);
  EXPECT_EQ(cameras, 1);
  scene.Registry().clear();
  EXPECT_EQ(other.Registry().view<Transform>().size(), 3u);
}

TEST_F(ResourceSceneTest, HandlesDefaultSceneAndMalformedHierarchy)
{
  std::ofstream(directory / "no-default.gltf")
      << R"({"asset":{"version":"2.0"},"nodes":[{}],"scenes":[{"name":"Only","nodes":[0]}]})";
  EXPECT_EQ(Resource::LoadScene(directory / "no-default.gltf")
                .GetDefaultScene()
                .Name(),
            "Only");
  std::ofstream(directory / "empty.gltf") << R"({"asset":{"version":"2.0"}})";
  auto empty = Resource::LoadScene(directory / "empty.gltf");
  EXPECT_TRUE(empty.GetScenes().empty());
  EXPECT_THROW(empty.GetDefaultScene(), std::out_of_range);
  std::ofstream(directory / "cycle.gltf")
      << R"({"asset":{"version":"2.0"},"nodes":[{"children":[0]}],"scenes":[{"nodes":[0]}]})";
  EXPECT_THROW(Resource::LoadScene(directory / "cycle.gltf").GetDefaultScene(),
               std::invalid_argument);
}

TEST(ResourceHandles, TextureBindingsAreSharedAndCpuOnly)
{
  auto texture = Resource::LoadTexture(
      "missing.png", {.magnification = SamplerFilter::Nearest});
  auto material = Resource::Material::FromData();
  auto alias = material;
  material.SetTexture("albedo", texture);
  EXPECT_EQ(alias.GetTexture("albedo"), texture);
  EXPECT_EQ(alias.GetRevision(), 2u);
  EXPECT_EQ(texture.GetSampler().magnification, SamplerFilter::Nearest);
  EXPECT_EQ(std::get<Resource::FileImage>(texture.GetImage().GetSource()).path,
            "missing.png");
  EXPECT_THROW(material.SetTexture("bad", {}), std::invalid_argument);
  EXPECT_EQ(material.GetRevision(), 2u);
  auto model = Resource::Model::FromData(Mesh3D{});
  EXPECT_TRUE(std::holds_alternative<OwnedModel>(model.GetSource()));
  EXPECT_NE(model.GetID(), material.GetID());
  EXPECT_NE(texture.GetID(), texture.GetImage().GetID());
}
