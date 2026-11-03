#include <cstring>
#include <cstdlib>
#include "png_io.hpp"

#include <fstream>
#include <vector>

#include "dds_io.hpp"
#include "png.h"
#include "../fs_helper.hpp"

static aurora::Module Log("aurora::gfx::png");

namespace aurora::gfx::png {

struct PngStructs {
  png_structp pStruct;
  png_infop pInfo;
  std::ifstream file;

  ~PngStructs() {
    png_destroy_read_struct(&pStruct, &pInfo, nullptr);
  }
};

static void readPngData(png_structp png, png_bytep data, const size_t length) {
  auto structs = static_cast<PngStructs*>(png_get_io_ptr(png));
  structs->file.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(length));

  if (structs->file.eof() || structs->file.fail()) {
    png_error(png, "file read failed!");
  }
}

std::optional<ConvertedTexture>
load_png_file(const std::filesystem::path& path) noexcept {
  PngStructs structs{};

  structs.file = std::move(std::ifstream(path, std::ifstream::in | std::ifstream::binary));
  if (!structs.file) {
    Log.error("failed to open file: {}", fs_path_to_string(path));
    return std::nullopt;
  }

  structs.pStruct = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!structs.pStruct) {
    Log.error("png_create_read_struct failed");
    return std::nullopt;
  }

  structs.pInfo = png_create_info_struct(structs.pStruct);
  if (!structs.pInfo) {
    Log.error("png_create_info_struct failed");
    return std::nullopt;
  }

  // I'm scared of putting any locals after that setjmp.
  std::vector<png_bytep> rowPointers;
  ByteBuffer imageData{};
  png_uint_32 width, height;
  int bit_depth, color_type, interlace_type, compression_type, filter_type;
  size_t rowBytes;
  int i;

  if (setjmp(png_jmpbuf(structs.pStruct))) {
    Log.error("libpng encountered an error");
    return std::nullopt;
  }

  png_set_read_fn(structs.pStruct, &structs, readPngData);
  png_read_info(structs.pStruct, structs.pInfo);

  if (!png_get_IHDR(structs.pStruct, structs.pInfo, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_type)) {
    Log.error("libpng unable to read IHDR");
    return std::nullopt;
  }

  // Always read as RGBA8.
  png_set_gray_to_rgb(structs.pStruct);
  png_set_filler(structs.pStruct, 0xFF, PNG_FILLER_AFTER);
  png_set_expand(structs.pStruct);
  png_set_strip_16(structs.pStruct);

  png_read_update_info(structs.pStruct, structs.pInfo);
  rowBytes = png_get_rowbytes(structs.pStruct, structs.pInfo);
  rowPointers.resize(height);

  imageData.append_zeroes(rowBytes * height);

  for (i = 0; i < height; i++) {
    rowPointers[i] = imageData.data() + i * rowBytes;
  }

  png_read_image(structs.pStruct, rowPointers.data());
  png_read_end(structs.pStruct, nullptr);

  return ConvertedTexture{
    .format = wgpu::TextureFormat::RGBA8Unorm,
    .width = width,
    .height = height,
    .mips = 1,
    .data = std::move(imageData)
  };
}
}
// Runtime UI helper: decode a PNG to tightly packed RGBA8 (free with std::free).
extern "C" uint8_t* aurora_load_png_rgba(const char* path, uint32_t* width, uint32_t* height) {
  auto tex = aurora::gfx::png::load_png_file(std::filesystem::u8path(path));
  if (!tex || tex->data.size() == 0) return nullptr;
  auto* out = static_cast<uint8_t*>(std::malloc(tex->data.size()));
  if (!out) return nullptr;
  std::memcpy(out, tex->data.data(), tex->data.size());
  *width = tex->width;
  *height = tex->height;
  return out;
}

namespace {
void writePngData(png_structp png, png_bytep data, size_t length) {
  auto* out = static_cast<std::ofstream*>(png_get_io_ptr(png));
  out->write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
  if (!*out) {
    png_error(png, "file write failed!");
  }
}

void flushPngData(png_structp png) { static_cast<std::ofstream*>(png_get_io_ptr(png))->flush(); }
} // namespace

// Screenshots: write tightly packed 8-bit RGB rows (top row first) as a PNG. path is UTF-8.
extern "C" bool aurora_write_png_rgb(const char* path, const uint8_t* rgb, uint32_t width, uint32_t height) {
  std::ofstream out(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png) {
    return false;
  }
  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_write_struct(&png, nullptr);
    return false;
  }
  // No locals after the setjmp (see load_png_file).
  std::vector<png_bytep> rows(height);
  for (uint32_t y = 0; y < height; ++y) {
    rows[y] = const_cast<png_bytep>(rgb + static_cast<size_t>(y) * width * 3);
  }
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    return false;
  }
  png_set_write_fn(png, &out, writePngData, flushPngData);
  png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  png_write_image(png, rows.data());
  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);
  out.close();
  return !out.fail();
}
