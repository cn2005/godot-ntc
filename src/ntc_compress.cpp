#include "ntc_compress.h"

#include <libntc/ntc.h>

#include <algorithm>
#include <mutex>

namespace {

std::mutex g_cuda_mutex;

ntc::BlockCompressedFormat bc_from_int(int value) {
	switch (value) {
		case 1:
			return ntc::BlockCompressedFormat::BC1;
		case 4:
			return ntc::BlockCompressedFormat::BC4;
		case 5:
			return ntc::BlockCompressedFormat::BC5;
		case 7:
			return ntc::BlockCompressedFormat::BC7;
		default:
			return ntc::BlockCompressedFormat::None;
	}
}

int mip_count_for(int width, int height) {
	int mips = 1;
	int w = width;
	int h = height;
	while (w > 1 || h > 1) {
		w = std::max(1, w / 2);
		h = std::max(1, h / 2);
		++mips;
	}
	return mips;
}

} // namespace

bool ntc_cuda_available(std::string &out_error) {
	std::lock_guard<std::mutex> lock(g_cuda_mutex);
	ntc::ContextParameters params;
	params.cudaDevice = 0;
	params.graphicsApi = ntc::GraphicsAPI::None;
	params.enableCooperativeVector = false;

	ntc::IContext *ctx = nullptr;
	const ntc::Status st = ntc::CreateContext(&ctx, params);
	if (ctx) {
		ntc::DestroyContext(ctx);
	}
	if (st == ntc::Status::Ok) {
		out_error.clear();
		return true;
	}
	out_error = std::string("CreateContext: ") + ntc::StatusToString(st) + " " + ntc::GetLastErrorMessage();
	return false;
}

bool ntc_compress_texture_set(const NtcCompressRequest &request, std::atomic<bool> *cancel, NtcCompressProgress *progress,
		std::string &out_error) {
	if (request.textures.empty()) {
		out_error = "No textures to compress.";
		return false;
	}
	if (request.width <= 0 || request.height <= 0) {
		out_error = "Invalid texture set size.";
		return false;
	}
	if (request.output_path.empty()) {
		out_error = "Missing output path.";
		return false;
	}

	int total_channels = 0;
	for (const NtcCompressChannel &ch : request.textures) {
		total_channels += ch.num_channels;
	}
	if (total_channels <= 0 || total_channels > NTC_MAX_CHANNELS) {
		out_error = "Channel count must be 1..16, got " + std::to_string(total_channels);
		return false;
	}

	std::lock_guard<std::mutex> lock(g_cuda_mutex);

	auto log = [&](const char *msg) {
		if (request.on_log) {
			NtcCompressProgress snap;
			if (progress) {
				snap = *progress;
			}
			request.on_log(snap, msg);
		}
	};

	log("Creating CUDA context…");

	ntc::ContextParameters params;
	params.cudaDevice = 0;
	params.graphicsApi = ntc::GraphicsAPI::None;
	params.enableCooperativeVector = false;

	ntc::IContext *ctx = nullptr;
	ntc::Status st = ntc::CreateContext(&ctx, params);
	if (st != ntc::Status::Ok || !ctx) {
		out_error = std::string("CUDA CreateContext failed: ") + ntc::StatusToString(st) + " " +
				ntc::GetLastErrorMessage();
		return false;
	}
	log("CUDA context ready");

	const int full_chain = mip_count_for(request.width, request.height);
	int mips = request.mips > 0 ? request.mips : full_chain;
	if (mips < 1) {
		mips = 1;
	}
	if (mips > NTC_MAX_MIPS) {
		mips = NTC_MAX_MIPS;
	}
	ntc::TextureSetDesc desc;
	desc.width = request.width;
	desc.height = request.height;
	desc.channels = total_channels;
	desc.mips = mips;

	ntc::TextureSetFeatures features;
	features.stagingBytesPerPixel = 4;
	features.enableCompression = true;

	ntc::ITextureSet *texture_set = nullptr;
	st = ctx->CreateTextureSet(desc, features, &texture_set);
	if (st != ntc::Status::Ok || !texture_set) {
		out_error = std::string("CreateTextureSet failed: ") + ntc::StatusToString(st) + " " +
				ntc::GetLastErrorMessage();
		ntc::DestroyContext(ctx);
		return false;
	}

	auto cleanup = [&]() {
		if (texture_set) {
			ctx->DestroyTextureSet(texture_set);
			texture_set = nullptr;
		}
		if (ctx) {
			ntc::DestroyContext(ctx);
			ctx = nullptr;
		}
	};

	float actual_bpp = 0.f;
	ntc::LatentShape shape;
	st = ntc::PickLatentShape(request.bits_per_pixel, actual_bpp, shape);
	if (st != ntc::Status::Ok) {
		out_error = std::string("PickLatentShape failed: ") + ntc::StatusToString(st) + " " +
				ntc::GetLastErrorMessage();
		cleanup();
		return false;
	}
	st = texture_set->SetLatentShape(shape);
	if (st != ntc::Status::Ok) {
		out_error = std::string("SetLatentShape failed: ") + ntc::StatusToString(st) + " " +
				ntc::GetLastErrorMessage();
		cleanup();
		return false;
	}

	int first_channel = 0;
	for (const NtcCompressChannel &ch : request.textures) {
		ntc::ITextureMetadata *meta = texture_set->AddTexture();
		if (!meta) {
			out_error = "AddTexture failed.";
			cleanup();
			return false;
		}
		meta->SetName(ch.name.c_str());
		st = meta->SetChannels(first_channel, ch.num_channels);
		if (st != ntc::Status::Ok) {
			out_error = std::string("SetChannels failed: ") + ntc::StatusToString(st) + " " +
					ntc::GetLastErrorMessage();
			cleanup();
			return false;
		}
		meta->SetChannelFormat(ntc::ChannelFormat::UNORM8);
		meta->SetBlockCompressedFormat(bc_from_int(ch.bc_format));
		meta->SetRgbColorSpace(ch.srgb ? ntc::ColorSpace::sRGB : ntc::ColorSpace::Linear);
		meta->SetAlphaColorSpace(ntc::ColorSpace::Linear);

		ntc::ColorSpace spaces[4];
		for (int c = 0; c < 4; ++c) {
			spaces[c] = (ch.srgb && c < 3) ? ntc::ColorSpace::sRGB : ntc::ColorSpace::Linear;
		}

		const int written_mips = std::min(mips, int(ch.mip_rgba8.size()));
		if (written_mips < 1) {
			out_error = "Texture " + ch.name + " has no mip data.";
			cleanup();
			return false;
		}
		for (int mip = 0; mip < written_mips; ++mip) {
			const int mw = std::max(1, request.width >> mip);
			const int mh = std::max(1, request.height >> mip);
			const size_t expected = size_t(mw) * size_t(mh) * 4;
			if (ch.mip_rgba8[mip].size() < expected) {
				out_error = "Texture " + ch.name + " mip " + std::to_string(mip) + " is truncated.";
				cleanup();
				return false;
			}
			ntc::WriteChannelsParameters write;
			write.mipLevel = mip;
			write.firstChannel = first_channel;
			write.numChannels = ch.num_channels;
			write.pData = ch.mip_rgba8[mip].data();
			write.addressSpace = ntc::AddressSpace::Host;
			write.width = mw;
			write.height = mh;
			write.pixelStride = 4;
			write.rowPitch = size_t(mw) * 4;
			write.channelFormat = ntc::ChannelFormat::UNORM8;
			write.srcColorSpaces = spaces;
			write.dstColorSpaces = spaces;
			st = texture_set->WriteChannels(write);
			if (st != ntc::Status::Ok) {
				out_error = std::string("WriteChannels(") + ch.name + " mip " + std::to_string(mip) +
						") failed: " + ntc::StatusToString(st) + " " + ntc::GetLastErrorMessage();
				cleanup();
				return false;
			}
		}
		first_channel += ch.num_channels;
		log((std::string("Wrote ") + ch.name + " (" + std::to_string(written_mips) + " mips)").c_str());
	}

	bool need_generate = false;
	for (const NtcCompressChannel &ch : request.textures) {
		if (int(ch.mip_rgba8.size()) < mips) {
			need_generate = true;
			break;
		}
	}
	if (need_generate && mips > 1) {
		log("Generating missing mips from mip 0…");
		st = texture_set->GenerateMips();
		if (st != ntc::Status::Ok) {
			out_error = std::string("GenerateMips failed: ") + ntc::StatusToString(st) + " " +
					ntc::GetLastErrorMessage();
			cleanup();
			return false;
		}
	}

	ntc::CompressionSettings settings;
	settings.trainingSteps = std::max(1000, request.training_steps);
	// Short iterations keep Windows TDR from killing Godot+CUDA on 4K sets.
	settings.stepsPerIteration = 100;
	log("BeginCompression…");
	st = texture_set->BeginCompression(settings);
	if (st != ntc::Status::Ok) {
		out_error = std::string("BeginCompression failed: ") + ntc::StatusToString(st) + " " +
				ntc::GetLastErrorMessage();
		cleanup();
		return false;
	}

	ntc::CompressionStats stats;
	do {
		if (cancel && cancel->load()) {
			texture_set->AbortCompression();
			out_error = "Compression cancelled.";
			cleanup();
			return false;
		}
		st = texture_set->RunCompressionSteps(&stats);
		if (progress) {
			progress->step = stats.currentStep;
			progress->total = settings.trainingSteps;
			progress->loss = stats.loss;
		}
		log("Training");
		if (st != ntc::Status::Incomplete && st != ntc::Status::Ok) {
			out_error = std::string("RunCompressionSteps failed: ") + ntc::StatusToString(st) + " " +
					ntc::GetLastErrorMessage();
			cleanup();
			return false;
		}
	} while (st == ntc::Status::Incomplete);

	log("FinalizeCompression…");
	st = texture_set->FinalizeCompression();
	if (st != ntc::Status::Ok) {
		out_error = std::string("FinalizeCompression failed: ") + ntc::StatusToString(st) + " " +
				ntc::GetLastErrorMessage();
		cleanup();
		return false;
	}

	log("Saving .ntc…");
	st = texture_set->SaveToFile(request.output_path.c_str());
	if (st != ntc::Status::Ok) {
		out_error = std::string("SaveToFile failed: ") + ntc::StatusToString(st) + " " + ntc::GetLastErrorMessage();
		cleanup();
		return false;
	}

	cleanup();
	out_error.clear();
	return true;
}
