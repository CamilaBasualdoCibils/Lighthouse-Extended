#include <Lighthouse/Extended/Rendering/SceneRenderProviders.hpp>
#include <Lighthouse/Extended/World/Entity/Transform.hpp>
#include <gtest/gtest.h>

using namespace Lighthouse;

TEST(RenderCollection, ResourceRequestsRemainStubs)
{
  ResourceRequests requests;
  EXPECT_FALSE(requests.LoadMesh({}));
  EXPECT_FALSE(requests.LoadMaterial({}));
  EXPECT_FALSE(requests.LoadTexture({}));
  EXPECT_FALSE(requests.LoadImage(Image::FromFile("unused.png")));
}

TEST(RenderCollection, TracksOnlyChangedSceneInstances)
{
  Scene scene("test");
  SceneRenderData extension(scene);
  ResourceRequests requests;
  const auto& draws = extension.GetDraws();
  std::uint64_t frame{};
  const auto collect = [&] {
    RenderUpdateContext context{requests, 0.016f, ++frame};
    extension.Update(context);
  };
  auto parent = scene.CreateEntity("parent");
  parent.Add<Transform>().SetPos({2, 0, 0});
  auto child = scene.CreateEntity("child");
  auto& transform = child.Add<Transform>();
  transform.SetPos({1, 0, 0});
  transform.SetParent(parent.ID());
  auto material = MaterialResource::FromData();
  child.Add<MeshRender>(ModelResource::FromData(Mesh3D{}), material);

  collect();
  ASSERT_EQ(draws.GetInstances().size(), 1);
  const auto handle = draws.GetInstances().begin()->first;
  EXPECT_EQ(draws.GetInstances().at(handle).worldTransform[3].x, 3);
  EXPECT_EQ(draws.GetInstances().at(handle).revision, 1);
  collect();
  EXPECT_EQ(draws.GetInstances().at(handle).revision, 1);

  parent.Get<Transform>().SetPos({4, 0, 0});
  collect();
  EXPECT_EQ(draws.GetInstances().at(handle).worldTransform[3].x, 5);
  EXPECT_EQ(draws.GetInstances().at(handle).revision, 2);

  material.SetTexture("baseColor", TextureResource::FromImage(Image::FromFile("unused.png")));
  collect();
  EXPECT_EQ(draws.GetInstances().at(handle).revision, 3);
  collect();
  EXPECT_EQ(draws.GetInstances().at(handle).revision, 3);

  child.Get<Transform>().RemoveParent();
  collect();
  EXPECT_EQ(draws.GetInstances().at(handle).worldTransform[3].x, 1);
  EXPECT_EQ(draws.GetInstances().at(handle).revision, 4);
  scene.Registry().remove<MeshRender>(child.ID());
  collect();
  EXPECT_TRUE(draws.GetInstances().empty());
}

TEST(RenderCollection, ScenesOwnIndependentCollections)
{
  Scene first("first"), second("second");
  SceneRenderData a(first), b(second);
  ResourceRequests requests;
  auto e = first.CreateEntity("one");
  e.Add<Transform>();
  e.Add<MeshRender>();
  auto f = second.CreateEntity("two");
  f.Add<Transform>();
  f.Add<MeshRender>();
  RenderUpdateContext context{requests, 0.016f, 1};
  a.Update(context); b.Update(context);
  ASSERT_EQ(a.GetDraws().GetInstances().size(), 1);
  ASSERT_EQ(b.GetDraws().GetInstances().size(), 1);
  EXPECT_NE(a.GetDraws().GetInstances().begin()->first, b.GetDraws().GetInstances().begin()->first);
  first.Registry().destroy(e.ID());
  ++context.frameId;
  a.Update(context);
  EXPECT_TRUE(a.GetDraws().GetInstances().empty());
  EXPECT_EQ(b.GetDraws().GetInstances().size(), 1);
  second.Registry().destroy(f.ID());
  b.Update(context);
  EXPECT_TRUE(b.GetDraws().GetInstances().empty());
}

TEST(RenderCollection, DetectsTransformCycles)
{
  Scene scene("cycle");
  SceneRenderData extension(scene);
  auto a = scene.CreateEntity("a"), b = scene.CreateEntity("b");
  a.Add<Transform>().SetParent(b.ID());
  b.Add<Transform>().SetParent(a.ID());
  a.Add<MeshRender>();
  ResourceRequests requests;
  RenderUpdateContext context{requests, 0.016f, 1};
  EXPECT_THROW(extension.Update(context), std::logic_error);
}

TEST(RenderCollection, RemovedHandlesCannotUpdateAnotherInstance)
{
  DrawCollector draws;
  const auto removed = draws.Add({});
  draws.Remove(removed);
  const auto added = draws.Add({});
  EXPECT_NE(removed, added);
  EXPECT_THROW(draws.Update(removed, {}), std::out_of_range);
  DrawCollector other;
  const auto foreign = other.Add({});
  EXPECT_THROW(draws.Update(foreign, {}), std::out_of_range);
}
