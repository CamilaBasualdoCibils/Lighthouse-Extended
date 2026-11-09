#include <Lighthouse/Extended/Resources/Resource.hpp>
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>

using namespace Lighthouse;
namespace
{
class ResourceModelTest : public testing::Test
{
protected:
  std::filesystem::path directory = std::filesystem::temp_directory_path() /
      ("lighthouse-model-" + std::to_string(
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
}

TEST_F(ResourceModelTest, OwnsLoadedAssetAndPreservesScenes)
{
  const auto path = directory / "model.gltf";
  auto model = Resource::LoadModel(path);
  auto alias = model;
  auto source = std::get<Resource::GLTFModel>(model.GetSource());
  ASSERT_NE(source.asset, nullptr);
  EXPECT_EQ(source.path, path);
  ASSERT_EQ(source.asset->scenes.size(), 2u);
  EXPECT_EQ(source.asset->scenes[1].name, "Second");
  EXPECT_EQ(source.asset->defaultScene, 1u);
  EXPECT_EQ(model.GetRevision(), 1u);
  EXPECT_EQ(model, alias);
  EXPECT_NE(model.GetID(), Resource::LoadModel(path).GetID());
  const auto id = model.GetID();
  alias.UpdateGLTF(path);
  EXPECT_EQ(model.GetID(), id);
  EXPECT_EQ(model.GetRevision(), 2u);
  EXPECT_NE(source.asset, std::get<Resource::GLTFModel>(model.GetSource()).asset);
  std::filesystem::remove(path);
  EXPECT_EQ(source.asset->nodes[0].name, "Root");
}

TEST_F(ResourceModelTest, LoadErrorsLeaveExistingResourceUnchanged)
{
  auto model = Resource::LoadModel(directory / "model.gltf");
  auto source = std::get<Resource::GLTFModel>(model.GetSource());
  EXPECT_THROW(Resource::LoadModel({}), std::invalid_argument);
  EXPECT_THROW(Resource::LoadModel(directory / "missing.gltf"), std::runtime_error);
  EXPECT_THROW(Resource::LoadModel(directory / "bad.gltf"), std::runtime_error);
  EXPECT_THROW(model.UpdateGLTF(directory / "bad.gltf"), std::runtime_error);
  EXPECT_EQ(model.GetRevision(), 1u);
  EXPECT_EQ(source.asset, std::get<Resource::GLTFModel>(model.GetSource()).asset);
}
