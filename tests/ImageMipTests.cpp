#include <Lighthouse/Extended/Resources/Resource.hpp>
#include <gtest/gtest.h>
#include <cstring>
#include <fstream>
using namespace Lighthouse;
namespace {
ImageData Authored() {
  return {ImageFormat::RGBA8,{{4,2,1,std::vector<std::byte>(32,std::byte{20})},
                            {2,1,1,std::vector<std::byte>(8,std::byte{180})}}};
}

}
TEST(ImageMips, PreservesAllLevelsInOwnedDataAndUploadDescriptions) {
  auto source=Authored();auto copy=source;
  EXPECT_NE(source.mips[1].data.data(),copy.mips[1].data.data());
  EXPECT_EQ(source.GetResolution(1),UVec2(2,1));
  const auto desc=source.ToImageDesc();EXPECT_EQ(desc.mipLevels,2);ASSERT_EQ(desc.initialMips.size(),2);
  EXPECT_EQ(desc.initialMips[1].data(),source.mips[1].data.data());
  EXPECT_TRUE(desc.initialData.empty());
  EXPECT_THROW(ImageData(ImageFormat::RGBA8,{}),std::invalid_argument);
  EXPECT_THROW(ImageData(ImageFormat::RGBA8,{{4,2,1,std::vector<std::byte>(32)},
                                             {3,1,1,std::vector<std::byte>(12)}}),std::invalid_argument);
}
TEST(ImageMips, PoliciesDistinguishAuthoredAndGeneratedMips) {
  EXPECT_EQ(ApplyImageMipPolicy(Authored(),MipPolicy::None).GetMipCount(),1);
  const auto loaded=ApplyImageMipPolicy(Authored(),MipPolicy::Load);
  EXPECT_EQ(loaded.GetMipCount(),2);EXPECT_EQ(loaded.GetData(1)[0],std::byte{180});
  const auto automatic=ApplyImageMipPolicy(Authored(),MipPolicy::Auto);
  EXPECT_EQ(automatic.GetMipCount(),2);EXPECT_EQ(automatic.GetData(1)[0],std::byte{180});
  const auto generated=ApplyImageMipPolicy(Authored(),MipPolicy::Generate);
  EXPECT_EQ(generated.GetMipCount(),3);EXPECT_EQ(generated.GetData(1)[0],std::byte{20});
  auto base=ApplyImageMipPolicy(Authored(),MipPolicy::None);
  EXPECT_EQ(ApplyImageMipPolicy(base,MipPolicy::Auto).GetMipCount(),3);
  EXPECT_EQ(ApplyImageMipPolicy(base,MipPolicy::Load).GetMipCount(),1);
}
TEST(ImageMips, OddDimensionsAndVolumesIncludeAllSourcePixels) {
  auto image=GenerateImageMipmaps(ImageData({3,1},ImageFormat::R8,{std::byte{0},std::byte{0},std::byte{255}}));
  ASSERT_EQ(image.GetMipCount(),2);EXPECT_EQ(image.GetResolution(1),UVec2(1,1));EXPECT_EQ(image.GetData(1)[0],std::byte{85});
  auto volume=GenerateImageMipmaps(ImageData(ImageFormat::R8,{{2,2,2,{std::byte{0},std::byte{0},std::byte{0},std::byte{0},
    std::byte{200},std::byte{200},std::byte{200},std::byte{200}}}}));
  EXPECT_EQ(volume.GetDepth(1),1);EXPECT_EQ(volume.GetData(1)[0],std::byte{100});
}
TEST(ImageMips, FloatHDRAndSRGBUseTheCorrectFilteringSpace) {
  std::vector<std::byte> bytes(8);const float source[]{2,6};std::memcpy(bytes.data(),source,sizeof(source));
  auto image=GenerateImageMipmaps(ImageData({2,1},ImageFormat::R32f,bytes));float result;
  std::memcpy(&result,image.GetData(1).data(),4);EXPECT_FLOAT_EQ(result,4);
  const std::vector<std::byte> checker{std::byte{0},std::byte{0},std::byte{0},std::byte{255},
    std::byte{255},std::byte{255},std::byte{255},std::byte{255}};
  auto srgb=GenerateImageMipmaps(ImageData({2,1},ImageFormat::RGBA8Srgb,checker));
  EXPECT_NEAR(std::to_integer<int>(srgb.GetData(1)[0]),188,1);EXPECT_EQ(srgb.GetData(1)[3],std::byte{255});
}
TEST(ImageMips, FileOptionsLoadExistingMipsAndGenerateFromPNG) {
  const auto fixture=std::filesystem::path(LIGHTHOUSE_TEST_SOURCE_DIR)/"tests/Fixtures/Mips.ktx";
  auto loaded=Resource::LoadImage(fixture,{.mipPolicy=MipPolicy::Load}).Resolve();
  ASSERT_EQ(loaded.GetMipCount(),2);EXPECT_EQ(loaded.GetData(1)[0],std::byte{180});
  EXPECT_EQ(Resource::LoadImage(fixture).Resolve().GetMipCount(),2);
  EXPECT_EQ(Resource::LoadImage(fixture,{.mipPolicy=MipPolicy::None}).Resolve().GetMipCount(),1);
  auto regenerated=Resource::LoadImage(fixture,{.mipPolicy=MipPolicy::Generate}).Resolve();
  EXPECT_EQ(regenerated.GetMipCount(),3);EXPECT_EQ(regenerated.GetData(1)[0],std::byte{20});
  const auto png=fixture.parent_path()/"Decoder.png";
  EXPECT_EQ(Resource::LoadImage(png,{.mipPolicy=MipPolicy::Load}).Resolve().GetMipCount(),1);
  EXPECT_EQ(Resource::LoadImage(png,{.mipPolicy=MipPolicy::Generate}).Resolve().GetMipCount(),2);
  EXPECT_EQ(Resource::LoadImage(png).Resolve().GetMipCount(),2);
}
