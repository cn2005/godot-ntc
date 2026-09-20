#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct NtcCompressChannel {
	std::string semantic;
	std::string name;
	int num_channels = 0;
	bool srgb = false;
	int bc_format = 0;
	// One RGBA8 buffer per mip, mip 0 first. Sizes are (w>>m) * (h>>m) * 4.
	std::vector<std::vector<uint8_t>> mip_rgba8;
};

struct NtcCompressProgress {
	int step = 0;
	int total = 0;
	float loss = 0.f;
};

struct NtcCompressRequest {
	int width = 0;
	int height = 0;
	int mips = 1;
	float bits_per_pixel = 5.f;
	int training_steps = 20000;
	std::string output_path;
	std::vector<NtcCompressChannel> textures;
	std::function<void(const NtcCompressProgress &progress, const char *message)> on_log;
};

bool ntc_cuda_available(std::string &out_error);
bool ntc_compress_texture_set(const NtcCompressRequest &request, std::atomic<bool> *cancel, NtcCompressProgress *progress,
		std::string &out_error);
