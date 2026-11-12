#include <Lighthouse/Extended/Resources/Image.hpp>
#include <cstring>
#include <fstream>
#include <limits>
#include <array>
#include <bit>
#include <spdlog/spdlog.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include <stb_image.h>

namespace Lighthouse
{
namespace
{
std::vector<std::byte> ReadFile(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error("Cannot read render resource: " + path.string());
  const auto length = file.tellg();
  if (length < 0) throw std::runtime_error("Cannot determine resource length");
  std::vector<std::byte> bytes(static_cast<std::size_t>(length));
  file.seekg(0);
  if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
    throw std::runtime_error("Short read: " + path.string());
  return bytes;
}

template<class T>
ImageData DecodedPixels(T* pixels, int width, int height, ImageFormat format)
{
  std::unique_ptr<T, decltype(&stbi_image_free)> owned(pixels, stbi_image_free);
  if (!owned) {
    const auto* reason = stbi_failure_reason();
    throw std::runtime_error(std::string("Image decode failed: ") +
                             (reason ? reason : "unknown stb_image error"));
  }
  constexpr auto pixelSize = 4 * sizeof(T);
  if (width <= 0 || height <= 0 ||
      std::size_t(height) > std::numeric_limits<std::size_t>::max() / pixelSize / std::size_t(width))
    throw std::runtime_error("Invalid decoded image dimensions");
  std::vector<std::byte> bytes(std::size_t(width) * height * pixelSize);
  std::memcpy(bytes.data(), owned.get(), bytes.size());
  return {{unsigned(width), unsigned(height)}, format, std::move(bytes)};
}
// stb formats have one base level. KTX1 provides an explicit uncompressed
// mip container; compressed/cube/array assets require a dedicated decoder.
ImageData DecodeKTX(std::span<const std::byte> bytes)
{
  std::size_t offset=12;
  const auto word=[&]() {
    if (offset>bytes.size() || bytes.size()-offset<4) throw std::runtime_error("Truncated KTX image");
    std::uint32_t value;std::memcpy(&value,bytes.data()+offset,4);offset+=4;
    if constexpr (std::endian::native==std::endian::big) value=std::byteswap(value);
    return value;
  };
  if (word()!=0x04030201) throw std::runtime_error("Only little-endian KTX1 images are supported");
  const auto type=word(),typeSize=word(),external=word(),internal=word(),base=word();
  const auto width=word(),height=word(),depth=word(),arrays=word(),faces=word(),levels=word(),metadata=word();
  if (!width || !height || depth || arrays || faces!=1 || levels>32 || metadata>bytes.size()-offset)
    throw std::runtime_error("KTX image requires a non-array 2D texture");
  ImageFormat format;
  if (external!=0x1908 || base!=0x1908) throw std::runtime_error("KTX requires uncompressed RGBA pixels");
  if (type==0x1401 && typeSize==1 && (internal==0x8058 || internal==0x8c43))
    format=internal==0x8c43 ? ImageFormat::RGBA8Srgb : ImageFormat::RGBA8;
  else if (type==0x1406 && typeSize==4 && internal==0x8814) format=ImageFormat::RGBA32f;
  else throw std::runtime_error("Unsupported KTX pixel format (expected RGBA8 or RGBA32f)");
  offset+=metadata;
  std::vector<ImageMipData> mips;
  auto w=width,h=height;
  for (unsigned level=0;level<std::max(1u,levels);++level) {
    const auto size=word();
    if (std::uint64_t(w)*h*ImageData::BytesPerPixel(format)!=size || size>bytes.size()-offset)
      throw std::runtime_error("Invalid KTX mip size");
    std::vector<std::byte> data(bytes.begin()+offset,bytes.begin()+offset+size);
    if constexpr (std::endian::native==std::endian::big) {
      if (type==0x1406) for (std::size_t i=0;i<data.size();i+=4) std::reverse(data.begin()+i,data.begin()+i+4);
    }
    mips.push_back({w,h,1,std::move(data)});
    offset+=size;
    const auto padding=(4-size%4)%4;
    if (padding>bytes.size()-offset) throw std::runtime_error("Truncated KTX mip padding");
    offset+=padding;w=std::max(1u,w/2);h=std::max(1u,h/2);
  }
  return {format,std::move(mips)};
}
constexpr std::array<unsigned char,12> ktxSignature{0xab,0x4b,0x54,0x58,0x20,0x31,0x31,0xbb,0x0d,0x0a,0x1a,0x0a};
ImageData DecodeEmbeddedImage(std::span<const std::byte> bytes)
{
  if (bytes.size()>=ktxSignature.size() && std::memcmp(bytes.data(),ktxSignature.data(),ktxSignature.size())==0) return DecodeKTX(bytes);
  if (bytes.empty() || bytes.size() > std::size_t(std::numeric_limits<int>::max()))
    throw std::runtime_error("Encoded image size is outside stb_image's supported range");
  const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
  const int length = static_cast<int>(bytes.size());
  int width = 0, height = 0, channels = 0;
  if (stbi_is_hdr_from_memory(data, length)) {
    auto* pixels = stbi_loadf_from_memory(data, length, &width, &height, &channels, STBI_rgb_alpha);
    return DecodedPixels(pixels, width, height, ImageFormat::RGBA32f);
  }
  auto* pixels = stbi_load_from_memory(data, length, &width, &height, &channels, STBI_rgb_alpha);
  return DecodedPixels(pixels, width, height, ImageFormat::RGBA8);
}
ImageData DecodeImageFile(const std::filesystem::path& path)
{
  // KTX has its own container decoder; ordinary images go straight to stb's
  // file API without buffering the entire encoded file in application memory.
  std::array<unsigned char, ktxSignature.size()> signature{};
  {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read render resource: " + path.string());
    file.read(reinterpret_cast<char*>(signature.data()), signature.size());
    if (file.gcount() == signature.size() && signature == ktxSignature)
      return DecodeKTX(ReadFile(path));
  }
  const auto filename = path.u8string();
  const auto* name = reinterpret_cast<const char*>(filename.c_str());
  int width = 0, height = 0, channels = 0;
  if (stbi_is_hdr(name)) {
    auto* pixels = stbi_loadf(name, &width, &height, &channels, STBI_rgb_alpha);
    return DecodedPixels(pixels, width, height, ImageFormat::RGBA32f);
  }
  auto* pixels = stbi_load(name, &width, &height, &channels, STBI_rgb_alpha);
  return DecodedPixels(pixels, width, height, ImageFormat::RGBA8);
}
} // namespace
ImageData Image::Resolve() const
{
  spdlog::info("Resolving image: {}", GetName());
  return std::visit([](const auto& source) -> ImageData {
    using T = std::decay_t<decltype(source)>;
    if constexpr (std::is_same_v<T, MemoryImage>) return *source.data;
    else if constexpr (std::is_same_v<T, GeneratedImage>) return source.generator();
    else if constexpr (std::is_same_v<T, FileImage>)
    {
      auto pixels = DecodeImageFile(source.path);
      if (source.options.format && *source.options.format != pixels.GetFormat())
        throw std::runtime_error("Requested image format does not match decoded pixel format");
      if (source.options.flipY) {
        for (auto& mip:pixels.mips) {
          const auto stride=std::size_t(mip.width)*ImageData::BytesPerPixel(pixels.format);
          for (unsigned z=0;z<mip.depth;++z) for (unsigned y=0;y<mip.height/2;++y)
            for (std::size_t x=0;x<stride;++x)
              std::swap(mip.data[(std::size_t(z)*mip.height+y)*stride+x],
                        mip.data[(std::size_t(z)*mip.height+mip.height-y-1)*stride+x]);
        }
      }
      return ApplyImageMipPolicy(std::move(pixels),source.options.mipPolicy);
    }
    else
    {
      const auto& data = source.asset->images.at(source.image).data;
      if (const auto* path = std::get_if<std::filesystem::path>(&data)) return ApplyImageMipPolicy(DecodeImageFile(*path),MipPolicy::Auto);
      return ApplyImageMipPolicy(DecodeEmbeddedImage(std::get<std::vector<std::byte>>(data)),MipPolicy::Auto);
    }
  }, GetSource());
}

} // namespace Lighthouse
