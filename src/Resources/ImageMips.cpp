#include <Lighthouse/Extended/Resources/Image.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Lighthouse
{
namespace
{
struct PixelLayout { unsigned channels, bytes; bool floating, srgb; };
PixelLayout Layout(ImageFormat format)
{
  switch (format) {
  case ImageFormat::R8: return {1,1,false,false};
  case ImageFormat::RG8: return {2,1,false,false};
  case ImageFormat::RGB8: return {3,1,false,false};
  case ImageFormat::RGBA8: case ImageFormat::BGRA8: return {4,1,false,false};
  case ImageFormat::RGBA8Srgb: case ImageFormat::BGRA8Srgb: return {4,1,false,true};
  case ImageFormat::R16f: return {1,2,true,false};
  case ImageFormat::RG16f: return {2,2,true,false};
  case ImageFormat::RGB16f: return {3,2,true,false};
  case ImageFormat::RGBA16f: return {4,2,true,false};
  case ImageFormat::R32f: return {1,4,true,false};
  case ImageFormat::RG32f: return {2,4,true,false};
  case ImageFormat::RGB32f: return {3,4,true,false};
  case ImageFormat::RGBA32f: return {4,4,true,false};
  default: throw std::invalid_argument("Mip generation requires an uncompressed color image");
  }
}
float Read(const std::byte* data, PixelLayout layout, unsigned channel)
{
  float value;
  if (!layout.floating) value=std::to_integer<unsigned>(*data)/255.0f;
  else if (layout.bytes==2) { std::uint16_t half;std::memcpy(&half,data,2);value=glm::unpackHalf1x16(half); }
  else std::memcpy(&value,data,4);
  if (layout.srgb && channel<3) value=value<=0.04045f ? value/12.92f : std::pow((value+0.055f)/1.055f,2.4f);
  return value;
}
void Write(std::byte* data, PixelLayout layout, unsigned channel, float value)
{
  if (layout.srgb && channel<3) value=value<=0.0031308f ? value*12.92f : 1.055f*std::pow(value,1/2.4f)-0.055f;
  if (!layout.floating) *data=std::byte(std::clamp(int(std::lround(value*255)),0,255));
  else if (layout.bytes==2) { const auto half=glm::packHalf1x16(value);std::memcpy(data,&half,2); }
  else std::memcpy(data,&value,4);
}
}
ImageData GenerateImageMipmaps(const ImageData& source)
{
  source.Validate();
  const auto layout=Layout(source.format);
  const auto pixelSize=layout.bytes*layout.channels;
  ImageData result(source.format,{source.mips[0]});
  while (result.mips.back().width>1 || result.mips.back().height>1 || result.mips.back().depth>1) {
    const auto& previous=result.mips.back();
    ImageMipData next{std::max(1u,previous.width/2),std::max(1u,previous.height/2),std::max(1u,previous.depth/2),{}};
    next.data.resize(std::size_t(next.width)*next.height*next.depth*pixelSize);
    // Area filtering includes the entire footprint, including odd-sized edges.
    for (unsigned z=0;z<next.depth;++z) for (unsigned y=0;y<next.height;++y) for (unsigned x=0;x<next.width;++x) {
      const double x0=double(x)*previous.width/next.width,x1=double(x+1)*previous.width/next.width;
      const double y0=double(y)*previous.height/next.height,y1=double(y+1)*previous.height/next.height;
      const double z0=double(z)*previous.depth/next.depth,z1=double(z+1)*previous.depth/next.depth;
      for (unsigned c=0;c<layout.channels;++c) {
        double sum=0;
        for (unsigned iz=unsigned(z0);iz<unsigned(std::ceil(z1));++iz)
          for (unsigned iy=unsigned(y0);iy<unsigned(std::ceil(y1));++iy)
            for (unsigned ix=unsigned(x0);ix<unsigned(std::ceil(x1));++ix) {
              const double weight=(std::min(x1,double(ix+1))-std::max(x0,double(ix)))*
                (std::min(y1,double(iy+1))-std::max(y0,double(iy)))*
                (std::min(z1,double(iz+1))-std::max(z0,double(iz)));
              const auto offset=((std::size_t(iz)*previous.height+iy)*previous.width+ix)*pixelSize+c*layout.bytes;
              sum+=weight*Read(previous.data.data()+offset,layout,c);
            }
        const auto offset=((std::size_t(z)*next.height+y)*next.width+x)*pixelSize+c*layout.bytes;
        Write(next.data.data()+offset,layout,c,float(sum/((x1-x0)*(y1-y0)*(z1-z0))));
      }
    }
    result.mips.push_back(std::move(next));
  }
  return result;
}
ImageData ApplyImageMipPolicy(ImageData image, MipPolicy policy)
{
  image.Validate();
  switch (policy) {
  case MipPolicy::None: image.mips.resize(1);return image;
  case MipPolicy::Load: return image;
  case MipPolicy::Generate: return GenerateImageMipmaps(image);
  case MipPolicy::Auto: return image.mips.size()>1 ? std::move(image) : GenerateImageMipmaps(image);
  }
  throw std::invalid_argument("Invalid image mip policy");
}
} // namespace Lighthouse
