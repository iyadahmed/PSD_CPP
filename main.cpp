#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define IS_EVEN_OR_ZERO(x) (((x) & 1) == 0)
#define IS_ODD(x) (((x) & 1) == 1)

enum class ColorMode {
  Bitmap = 0,
  Grayscale = 1,
  Indexed = 2,
  RGB = 3,
  CMYK = 4,
  Multichannel = 7,
  Duotone = 8,
  Lab = 9,
};

enum class Compression {
  Raw = 0,
  RLE = 1,
  ZIP = 2,
  ZIPPrediction = 3,
};

struct FileHeader {
  char signature[4];
  uint16_t version;
  char reserved[6];
  uint16_t num_channels;
  uint32_t height;
  uint32_t width;
  uint16_t depth;
  ColorMode color_mode;
};

struct ImageResource {
  uint16_t id;
  std::string name;
  std::vector<char> data;
};

struct ChannelInfo {
  uint16_t id;
  uint32_t data_length;
};

struct Rect {
  uint32_t top, left, bottom, right;

  uint32_t calc_size() const
  {
    return (bottom - top) * (right - left);
  }

  uint32_t calc_num_scan_lines() const
  {
    return bottom - top;
  }
};

struct LayerMaskData {
  uint32_t length;
  Rect rect;
  uint8_t default_color;  // 0 or 255

  // bit flags
  union {
    uint8_t flags;

    struct {
      uint8_t position_relative_to_layer : 1;
      uint8_t layer_mask_disabled : 1;
      uint8_t invert_layer_mask_when_blending : 1;  // Obsolete
      uint8_t layer_mask_from_rendered_data : 1;
      uint8_t mask_has_parameters_applied_to_it : 1;
    };
  };

  // end of flags

  // mask parameters flags, only present if bit 4 of flags above is set
  union {

    uint8_t mask_parameters_flags;

    struct {
      uint8_t is_user_mask_density_present : 1;
      uint8_t is_user_mask_feather_present : 1;
      uint8_t is_vector_mask_density_present : 1;
      uint8_t is_vector_mask_feather_present : 1;
    };
  };

  // end of mask parameters flags

  uint8_t user_mask_density;
  double user_mask_feather;
  uint8_t vector_mask_density;
  double vector_mask_feather;

  uint16_t padding;    // Only present if data_size = 20. Otherwise the following is present
  uint8_t real_flags;  // Same as flags above
  uint8_t real_user_mask_background;  // 0 or 255
  Rect real_rect;
};

struct BlendingRange {
  uint32_t source, destination;
};

struct LayerBlendingRanges {
  uint32_t length;
  BlendingRange composite_gray_range;
  std::vector<BlendingRange> channel_blending_ranges;
};

struct LayerRecord {
  Rect rect;
  uint16_t num_channels;
  std::vector<ChannelInfo> channel_info;
  char blend_mode_signature[4];
  char blend_mode_key[4];
  uint8_t opacity;
  bool clipping;

  // bit flags
  union {
    uint8_t flags;

    struct {
      uint8_t transparency_protected : 1;
      uint8_t visible : 1;
      uint8_t obsolete : 1;
      uint8_t is_bit_4_useful : 1;
      uint8_t is_pixel_data_irrelevant : 1;
    };
  };

  // end of flags
  uint8_t filler;

  uint32_t length_of_extra_data;
  LayerMaskData layer_mask_data;
  LayerBlendingRanges layer_blending_ranges;
  std::string layer_name;
};

struct ChannelImageData {
  Compression compression;
  std::vector<char> data;
};

struct LayerInfo {
  uint32_t length;
  /* Layer count. If it is a negative number, its absolute value is the number of layers and the
   * first alpha channel contains the transparency data for the merged result. */
  int16_t layer_count;
  std::vector<LayerRecord> layer_records;
  std::vector<ChannelImageData> channel_image_data;
};

struct LayerMaskInfo {
  uint32_t length;
  LayerInfo layer_info;
};

struct PSDFile {
  FileHeader header;
  std::vector<char> color_mode_data;
  std::vector<ImageResource> image_resources;
  LayerMaskInfo layer_mask_info;
};

class InvalidSignature : public std::exception {
 public:
  const char *what() const throw()
  {
    return "Invalid signature";
  }
};

bool all_zeros(const char *data, size_t size)
{
  for (size_t i = 0; i < size; ++i) {
    if (data[i] != 0) {
      return false;
    }
  }
  return true;
}

uint8_t read_uint8(std::ifstream &in)
{
  uint8_t value;
  in.read(reinterpret_cast<char *>(&value), sizeof(uint8_t));
  return value;
}

bool read_bool(std::ifstream &in)
{
  bool value;
  in.read(reinterpret_cast<char *>(&value), sizeof(bool));
  return value;
}

uint16_t read_uint16(std::ifstream &in)
{
  uint16_t value;
  in.read(reinterpret_cast<char *>(&value), sizeof(uint16_t));
  if constexpr (std::endian::native == std::endian::little) {
    value = std::byteswap(value);
  }
  return value;
}

uint32_t read_uint32(std::ifstream &in)
{
  uint32_t value;
  in.read(reinterpret_cast<char *>(&value), sizeof(uint32_t));
  if constexpr (std::endian::native == std::endian::little) {
    value = std::byteswap(value);
  }
  return value;
}

int16_t read_int16(std::ifstream &in)
{
  int16_t value;
  in.read(reinterpret_cast<char *>(&value), sizeof(int16_t));
  if constexpr (std::endian::native == std::endian::little) {
    value = std::byteswap(value);
  }
  return value;
}

Rect read_rect(std::ifstream &in)
{
  Rect rect;
  rect.top = read_uint32(in);
  rect.left = read_uint32(in);
  rect.bottom = read_uint32(in);
  rect.right = read_uint32(in);
  return rect;
}

BlendingRange read_blending_range(std::ifstream &in)
{
  BlendingRange range;
  range.source = read_uint32(in);
  range.destination = read_uint32(in);
  return range;
}

double read_double(std::ifstream &in)
{
  double value;
  in.read(reinterpret_cast<char *>(&value), sizeof(double));
  if constexpr (std::endian::native == std::endian::little) {
    value = std::bit_cast<double>(std::byteswap(std::bit_cast<uint64_t>(value)));
  }
  return value;
}

void peek_n(std::ifstream &in, char *data, size_t size)
{
  in.read(data, size);
  in.seekg(-static_cast<int>(size), std::ios::cur);
}

FileHeader read_file_header(std::ifstream &in)
{
  FileHeader header;
  in.read(header.signature, 4);
  header.version = read_uint16(in);
  in.read(header.reserved, 6);
  header.num_channels = read_uint16(in);
  header.height = read_uint32(in);
  header.width = read_uint32(in);
  header.depth = read_uint16(in);
  header.color_mode = static_cast<ColorMode>(read_uint16(in));
  return header;
}

std::vector<char> read_color_mode_data(std::ifstream &in)
{
  std::vector<char> data;
  uint32_t size = read_uint32(in);
  if (size > 0) {
    data.resize(size);
    in.read(data.data(), data.size());
  }
  return data;
}

ImageResource read_image_resource(std::ifstream &in)
{
  char signature[4];
  peek_n(in, signature, 4);
  if (memcmp(signature, "8BIM", 4) != 0) {
    throw InvalidSignature();
  }
  in.read(signature, 4);
  ImageResource resource;
  resource.id = read_uint16(in);
  uint8_t name_length = read_uint8(in);
  uint8_t padded_name_length = name_length;
  // Make size even, specification says name is padded to even size (including length byte)
  // that's why we check if name_length is even, instead of odd,
  // we would have checked odd if we only padded name not including length byte
  // the specification also states that a 0 length string is two bytes
  // one length byte containing value 0, and a padding 0 byte to make total size even
  if (IS_EVEN_OR_ZERO(name_length)) {
    ++padded_name_length;
  }
  char name[256];
  in.read(name, padded_name_length);
  resource.name = std::string(name, name_length);
  uint32_t data_size = read_uint32(in);
  if (data_size > 0) {
    // Make size even, specification says data is padded to even size
    if (IS_ODD(data_size)) {
      ++data_size;
    }
    resource.data.resize(data_size);
    in.read(resource.data.data(), resource.data.size());
  }
  return resource;
}

std::vector<ImageResource> read_image_resources(std::ifstream &in)
{
  std::vector<ImageResource> resources;
  uint32_t image_resources_size = read_uint32(in);
  while (true) {
    try {
      resources.push_back(read_image_resource(in));
    }
    catch (InvalidSignature &e) {
      break;
    }
  }
  return resources;
}

ChannelInfo read_channel_info(std::ifstream &in)
{
  ChannelInfo info;
  info.id = read_uint16(in);
  info.data_length = read_uint32(in);
  return info;
}

LayerMaskData read_layer_mask_data(std::ifstream &in)
{
  LayerMaskData layer_mask_data;
  layer_mask_data.length = read_uint32(in);
  if (layer_mask_data.length == 0) {
    return layer_mask_data;
  }
  layer_mask_data.rect = read_rect(in);
  layer_mask_data.default_color = read_uint8(in);
  assert(layer_mask_data.default_color == 0 || layer_mask_data.default_color == 255);

  layer_mask_data.flags = read_uint8(in);
  if (layer_mask_data.mask_has_parameters_applied_to_it) {
    layer_mask_data.mask_parameters_flags = read_uint8(in);

    if (layer_mask_data.is_user_mask_density_present) {
      layer_mask_data.user_mask_density = read_uint8(in);
    }

    if (layer_mask_data.is_user_mask_feather_present) {
      layer_mask_data.user_mask_feather = read_double(in);
    }

    if (layer_mask_data.is_vector_mask_density_present) {
      layer_mask_data.vector_mask_density = read_uint8(in);
    }

    if (layer_mask_data.is_vector_mask_feather_present) {
      layer_mask_data.vector_mask_feather = read_double(in);
    }
  }
  if (layer_mask_data.length == 20) {
    layer_mask_data.padding = read_uint16(in);
  }
  else {
    layer_mask_data.real_flags = read_uint8(in);
    layer_mask_data.real_user_mask_background = read_uint8(in);
    assert(layer_mask_data.real_user_mask_background == 0 ||
           layer_mask_data.real_user_mask_background == 255);
    layer_mask_data.real_rect = read_rect(in);
  }

  return layer_mask_data;
}

LayerRecord read_layer_record(std::ifstream &in)
{
  LayerRecord record;
  record.rect = read_rect(in);
  record.num_channels = read_uint16(in);
  for (uint16_t i = 0; i < record.num_channels; i++) {
    record.channel_info.push_back(read_channel_info(in));
  }
  in.read(record.blend_mode_signature, 4);
  assert(std::string(record.blend_mode_signature, 4) == "8BIM");
  in.read(record.blend_mode_key, 4);

  record.opacity = read_uint8(in);
  record.clipping = read_bool(in);
  record.flags = read_uint8(in);
  record.filler = read_uint8(in);
  assert(record.filler == 0);

  record.length_of_extra_data = read_uint32(in);
  if (record.length_of_extra_data == 0) {
    return record;
  }
  // if (record.length_of_extra_data > 0) {
  //   std::vector<char> extra_data(record.length_of_extra_data);
  //   in.read(extra_data.data(), extra_data.size());
  // }
  auto offset = in.tellg();
  offset += record.length_of_extra_data;
  record.layer_mask_data = read_layer_mask_data(in);

  int num_read_bytes = 0;
  record.layer_blending_ranges.length = read_uint32(in);
  record.layer_blending_ranges.composite_gray_range = read_blending_range(in);
  num_read_bytes += sizeof(BlendingRange);
  for (uint16_t i = 0; i < record.num_channels; i++) {
    record.layer_blending_ranges.channel_blending_ranges.push_back(read_blending_range(in));
    num_read_bytes += sizeof(BlendingRange);
  }
  std::cout << num_read_bytes << " " << record.layer_blending_ranges.length << std::endl;
  assert(num_read_bytes == record.layer_blending_ranges.length);

  // Read Pascal style string padded to multiple of 4 bytes
  uint8_t layer_name_length = read_uint8(in);
  uint8_t layer_name_total_bytes = layer_name_length + 1;
  if ((layer_name_total_bytes % 4) != 0) {
    layer_name_total_bytes = ((layer_name_total_bytes / 4) + 1) * 4;
  }
  uint8_t layer_name_remaining_bytes = layer_name_total_bytes - 1;
  char layer_name[256];
  in.read(layer_name, layer_name_remaining_bytes);
  record.layer_name = std::string(layer_name, layer_name_length);
  std::cout << record.layer_name << std::endl;
  std::cout << record.layer_name.length() << std::endl;

  std::cout << "Remaining bytes: " << (offset - in.tellg()) << std::endl;

  in.seekg(offset);

  return record;
}

ChannelImageData read_channel_image_data(std::ifstream &in, const Rect &layer_rect)
{
  ChannelImageData channel_image_data;
  channel_image_data.compression = static_cast<Compression>(read_uint16(in));
  std::cout << "Compression type: " << (int)channel_image_data.compression << std::endl;
  std::cout << "Layer Size: " << layer_rect.calc_size() << std::endl;
  if (channel_image_data.compression == Compression::Raw) {
    channel_image_data.data.resize(layer_rect.calc_size());
    in.read(channel_image_data.data.data(), channel_image_data.data.size());
  }
  else if (channel_image_data.compression == Compression::RLE) {
    std::vector<uint16_t> byte_counts;
    for (uint32_t i = 0; i < layer_rect.calc_num_scan_lines(); i++) {
      byte_counts.push_back(read_uint16(in));
    }
    for (uint16_t n : byte_counts) {
      std::vector<char> bytes(n);
      in.read(bytes.data(), n);
    }
  }
  return channel_image_data;
}

LayerInfo read_layer_info(std::ifstream &in)
{
  LayerInfo info;
  info.length = read_uint32(in);
  info.layer_count = read_int16(in);
  int16_t layer_count = std::abs(info.layer_count);

  for (int16_t i = 0; i < layer_count; i++) {
    info.layer_records.push_back(read_layer_record(in));
  }
  for (const LayerRecord &r : info.layer_records) {
    for (int i = 0; i < r.num_channels; i++) {
      info.channel_image_data.push_back(read_channel_image_data(in, r.rect));
    }
  }
  return info;
}

LayerMaskInfo read_layer_and_mask_info(std::ifstream &in)
{
  LayerMaskInfo info;
  info.length = read_uint32(in);
  info.layer_info = read_layer_info(in);
  return info;
}

PSDFile read_psd(std::ifstream &in)
{
  PSDFile psd;
  psd.header = read_file_header(in);
  psd.color_mode_data = read_color_mode_data(in);
  psd.image_resources = read_image_resources(in);
  psd.layer_mask_info = read_layer_and_mask_info(in);
  return psd;
}

int main()
{
  std::ifstream in;
  in.exceptions(std::ifstream::failbit | std::ifstream::badbit | std::ifstream::eofbit);
  in.open("../images.psd", std::ifstream::binary);
  PSDFile psd = read_psd(in);
  std::cout << psd.image_resources.size() << std::endl;
  std::cout << psd.layer_mask_info.layer_info.layer_records.size() << std::endl;
  return 0;
}
