#include <Lighthouse/Extended/Resources/Resource.hpp>
#include <gtest/gtest.h>

using namespace Lighthouse;
static_assert(std::is_class_v<Resource>);
namespace
{
ImageData Pixel(std::byte value = std::byte{42})
{
  return ImageData({1, 1}, ImageFormat::R8, {value});
}
}

TEST(ResourceImage, FileReferenceIsLazyAndPreservesMetadata)
{
  const auto image = Resource::LoadImage(
      "does-not-exist/image.png", {ImageFormat::RGBA8, true});
  const auto source = std::get<Resource::FileImage>(image.GetSource());
  EXPECT_EQ(source.path, "does-not-exist/image.png");
  EXPECT_EQ(source.options.format, ImageFormat::RGBA8);
  EXPECT_TRUE(source.options.flipY);
  EXPECT_EQ(image.GetRevision(), 1u);
  EXPECT_NE(image.GetID(), 0u);
  EXPECT_NE(image.GetID(), Resource::LoadImage(source.path).GetID());
}

TEST(ResourceImage, CopiesShareUpdatesAndSnapshotsRetainOwnedPixels)
{
  auto pixels = Pixel();
  auto image = Resource::Image::FromData(pixels);
  auto alias = image;
  const auto id = image.GetID();
  const auto old = std::get<Resource::MemoryImage>(image.GetSource());
  EXPECT_NE(old.data->GetData().data(), pixels.GetData().data());
  alias.UpdateData(Pixel(std::byte{7}));
  EXPECT_EQ(image.GetID(), id);
  EXPECT_EQ(image, alias);
  EXPECT_EQ(image.GetRevision(), 2u);
  EXPECT_EQ(old.data->GetData()[0], std::byte{42});
  EXPECT_EQ(std::get<Resource::MemoryImage>(image.GetSource()).data->GetData()[0],
            std::byte{7});
}

TEST(ResourceImage, GeneratorsAreLazyAndCanBeInvalidatedOrReplaced)
{
  int calls = 0;
  auto image = Resource::Image::FromGenerator([&] {
    ++calls;
    return Pixel();
  });
  EXPECT_EQ(calls, 0);
  auto source = std::get<Resource::GeneratedImage>(image.GetSource());
  image.MarkDirty();
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(image.GetRevision(), 2u);
  EXPECT_EQ(source.generator().GetResolution().x, 1u);
  EXPECT_EQ(calls, 1);
  image.UpdateFile("new.png");
  EXPECT_TRUE(std::holds_alternative<Resource::FileImage>(image.GetSource()));
  image.UpdateGenerator([] { return Pixel(); });
  EXPECT_EQ(image.GetRevision(), 4u);
  EXPECT_TRUE(std::holds_alternative<Resource::GeneratedImage>(image.GetSource()));
}

TEST(ResourceImage, InvalidUpdatesLeaveResourceUnchanged)
{
  auto image = Resource::Image::FromData(Pixel());
  EXPECT_THROW(Resource::Image::FromFile({}), std::invalid_argument);
  EXPECT_THROW(Resource::Image::FromGenerator({}), std::invalid_argument);
  EXPECT_THROW(image.UpdateFile({}), std::invalid_argument);
  EXPECT_THROW(image.UpdateGenerator({}), std::invalid_argument);
  EXPECT_EQ(image.GetRevision(), 1u);
  EXPECT_TRUE(std::holds_alternative<Resource::MemoryImage>(image.GetSource()));
}
