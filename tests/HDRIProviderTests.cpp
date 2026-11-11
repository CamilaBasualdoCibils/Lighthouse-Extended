#include <Lighthouse/Extended/Rendering/EnvironmentProviders.hpp>
#include <Lighthouse/Extended/Resources/Resource.hpp>
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <fstream>
#include <cstdlib>
#include <numbers>
#include <random>
using namespace Lighthouse;
namespace {
ImageData Constant(Vec3 color, UVec2 size={8,4}) {
  std::vector<std::byte> bytes(size.x*size.y*16);
  const std::array<float,4> value{color.x,color.y,color.z,1};
  for (std::size_t i=0;i<bytes.size();i+=16) std::memcpy(bytes.data()+i,value.data(),16);
  return {size,ImageFormat::RGBA32f,std::move(bytes)};
}
std::array<float,4> Read(const ImageData& data,std::size_t pixel=0) {
  std::array<float,4> value;std::memcpy(value.data(),data.GetData().data()+pixel*16,16);return value;
}
struct Fixture {
  std::filesystem::path path;
  Fixture() {
    std::random_device random;
    for (;;) {
      const auto directory=std::filesystem::temp_directory_path()/
        ("lighthouse-hdr-"+std::to_string(random())+"-"+std::to_string(random()));
      if (std::filesystem::create_directory(directory)) { path=directory/"image.hdr"; break; }
    }
  }
  ~Fixture(){std::filesystem::remove_all(path.parent_path());}
  void Write(std::string_view header,std::span<const unsigned char> bytes) {
    std::ofstream file(path,std::ios::binary);file<<header;
    file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
  }
};
}
TEST(HDRI, ResourceLoadingIsLazyAndPreservesHDRAndFlipY) {
  Fixture file;
  auto image=Resource::LoadImage(file.path);
  EXPECT_THROW(image.Resolve(),std::runtime_error);
  const std::array<unsigned char,8> bytes{128,64,32,130,32,64,128,129};
  file.Write("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 1\n",bytes);
  const auto pixels=image.Resolve();EXPECT_EQ(pixels.GetFormat(),ImageFormat::RGBA32f);
  EXPECT_EQ(pixels.GetResolution(),UVec2(1,2));EXPECT_EQ(Read(pixels),(std::array<float,4>{2,1,0.5f,1}));
  const auto flipped=Resource::LoadImage(file.path,{.flipY=true}).Resolve();
  EXPECT_EQ(Read(flipped),Read(pixels,1));
  EXPECT_THROW(Resource::LoadImage(file.path,{.format=ImageFormat::RGBA8}).Resolve(),std::runtime_error);
}
TEST(HDRI, ScanlineRLEAndTruncationAreValidated) {
  Fixture file;
  const std::array<unsigned char,12> bytes{2,2,0,8,136,128,136,64,136,32,136,130};
  file.Write("#?RGBE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 8\n",bytes);
  auto image=Resource::LoadImage(file.path);const auto decoded=image.Resolve();
  EXPECT_EQ(Read(decoded,7),(std::array<float,4>{2,1,0.5f,1}));
  file.Write("#?RGBE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 8\n",std::span(bytes).first(9));
  EXPECT_THROW(image.Resolve(),std::runtime_error);
  const std::array<unsigned char,6> invalid{2,2,0,8,137,0};
  file.Write("#?RGBE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 8\n",invalid);
  EXPECT_THROW(image.Resolve(),std::runtime_error);
}
TEST(HDRI, ConstantRadianceIntegratesToPiTimesRadiance) {
  const auto result=ComputeHDRIIrradiance(Constant({2,1,0.5f}),{16,8},256);
  for(std::size_t i=0;i<16*8;++i) {
    const auto value=Read(result,i);
    EXPECT_NEAR(value[0],2*std::numbers::pi_v<float>,1e-4f);
    EXPECT_NEAR(value[1],std::numbers::pi_v<float>,1e-4f);
    EXPECT_NEAR(value[2],0.5f*std::numbers::pi_v<float>,1e-4f);
  }
  EXPECT_THROW(ComputeHDRIIrradiance(Constant({1,1,1}),{0,8}),std::invalid_argument);
  EXPECT_THROW(ComputeHDRIIrradiance(Constant({1,1,1}),{8,4},0),std::invalid_argument);
}
TEST(HDRI, EnvironmentCachesPixelsAndInvalidatesOnResourceRevision) {
  unsigned calls=0;auto image=Image::FromGenerator([&]{++calls;return Constant({1,2,3});});
  auto environment=std::make_shared<HDRIEnvironment>(image);
  HDRISkyProvider sky(environment);HDRIIndirectLightingProvider indirect(environment);
  EXPECT_EQ(calls,0);auto first=environment->Resolve();EXPECT_EQ(calls,1);
  EXPECT_EQ(first,environment->Resolve());EXPECT_EQ(calls,1);
  const auto revision=sky.GetConfigurationRevision();image.MarkDirty();
  EXPECT_NE(sky.GetConfigurationRevision(),revision);EXPECT_NE(indirect.GetConfigurationRevision(),revision);
  EXPECT_NE(first,environment->Resolve());EXPECT_EQ(calls,2);
  HDRIIndirectLightingOptions options;options.mode=IrradianceMode::Import;
  EXPECT_THROW(HDRIIndirectLightingProvider(environment,options),std::invalid_argument);
  options.irradiance=Resource::LoadImage("does-not-exist.irr.hdr");
  EXPECT_NO_THROW(HDRIIndirectLightingProvider(environment,options));
  EXPECT_EQ(calls,2); // Construction never opens a supplied map.
}
TEST(HDRI, NewportSourceAndExplicitIrradianceImport) {
  const char* path=std::getenv("LIGHTHOUSE_TEST_HDRI");
  if(!path) GTEST_SKIP()<<"Set LIGHTHOUSE_TEST_HDRI to exercise a local HDR fixture";
  auto source=Resource::LoadImage(path).Resolve();
  EXPECT_EQ(source.GetFormat(),ImageFormat::RGBA32f);
  auto result=ComputeHDRIIrradiance(source,{32,16},256);
  float maximum=0;for(std::size_t i=0;i<32*16;++i)maximum=std::max(maximum,Read(result,i)[0]);
  EXPECT_GT(maximum,0);
  if(const char* irr=std::getenv("LIGHTHOUSE_TEST_IRRADIANCE")) {
    auto environment=std::make_shared<HDRIEnvironment>(Resource::LoadImage(irr));
    EXPECT_EQ(environment->Resolve()->GetFormat(),ImageFormat::RGBA32f);
  }
}

TEST(ResourceImage, STBDecodesPNGAndJPEGToRGBA8) {
  const auto directory=std::filesystem::path(LIGHTHOUSE_TEST_SOURCE_DIR)/"tests/Fixtures";
  for (const auto* name : {"Decoder.png", "Decoder.jpg"}) {
    const auto image=Resource::LoadImage(directory/name).Resolve();
    ASSERT_EQ(image.GetFormat(),ImageFormat::RGBA8);
    ASSERT_EQ(image.GetResolution(),UVec2(2,2));
    for (std::size_t i=0;i<image.GetData().size();i+=4) {
      EXPECT_NEAR(std::to_integer<int>(image.GetData()[i]),64,2);
      EXPECT_NEAR(std::to_integer<int>(image.GetData()[i+1]),128,2);
      EXPECT_NEAR(std::to_integer<int>(image.GetData()[i+2]),192,2);
      EXPECT_EQ(image.GetData()[i+3],std::byte{255});
    }
  }
}
