#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <string>
#include <vector>

class NtcVkDevice;
namespace ntc {
class IContext;
}

struct NtcDecodedTexture {
	godot::String name;
	int first_channel = 0;
	int num_channels = 0;
	int width = 0;
	int height = 0;
	int mips = 1;
	bool srgb = false;
	godot::Image::Format image_format = godot::Image::FORMAT_RGBA8;
	godot::PackedByteArray data;
};

struct NtcDecodeResult {
	int width = 0;
	int height = 0;
	int mips = 1;
	int channels = 0;
	float bits_per_pixel = 0.f;
	godot::String weight_type;
	std::vector<NtcDecodedTexture> textures;
};

bool ntc_decode_file_a1(NtcVkDevice &vk_dev, ntc::IContext *ctx, const uint8_t *file_bytes, size_t file_size,
		NtcDecodeResult &out, std::string &out_error);
