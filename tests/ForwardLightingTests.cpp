#include <Lighthouse/Extended/Rendering/ForwardRendering.hpp>
#include <Lighthouse/Extended/Rendering/SceneRenderStore.hpp>
#include <Lighthouse/Extended/World/Entity/Light.hpp>
#include <Lighthouse/Extended/World/Entity/Transform.hpp>
#include <gtest/gtest.h>
using namespace Lighthouse;
TEST(ForwardLighting, UsesSceneComponentsAndParentTransforms)
{
  Scene scene("lights");auto store=std::make_shared<SceneRenderStore>(scene);
  DefaultForwardLightingProvider provider(store);
  auto parent=scene.CreateEntity("parent");parent.Add<Transform>().SetPos({3,2,1});
  auto point=scene.CreateEntity("point");point.Add<Transform>().SetParent(parent.ID());
  auto& p=point.Add<Light>();p.type=LightType::Point;p.color={0.2f,0.4f,0.6f};p.intensity=5;p.range=8;
  auto spot=scene.CreateEntity("spot");spot.Add<Transform>();auto& s=spot.Add<Light>();s.type=LightType::Spot;
  auto disabled=scene.CreateEntity("disabled");disabled.Add<Transform>();disabled.Add<Light>().enabled=false;
  const auto lights=provider.GetLights();ASSERT_EQ(lights.size(),2);
  bool foundPoint=false,foundSpot=false;
  for(const auto& light:lights){
    if(light.directionType.w==1){foundPoint=true;EXPECT_EQ(light.positionRange,Vec4(3,2,1,8));EXPECT_EQ(light.colorIntensity,Vec4(0.2f,0.4f,0.6f,5));}
    if(light.directionType.w==2){foundSpot=true;EXPECT_FLOAT_EQ(light.spotCosines.x,std::cos(glm::radians(s.innerAngle)));}
  }
  EXPECT_TRUE(foundPoint);EXPECT_TRUE(foundSpot);
  p.range=0;EXPECT_THROW(provider.GetLights(),std::invalid_argument);
}
