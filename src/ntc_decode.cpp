#include "ntc_decode.h"
#include "ntc_vk.h"

#include <libntc/ntc.h>
#include <libntc/shaders/Bindings.h>
#include <libntc/shaders/DecompressConstants.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

using godot::Image;

namespace {

constexpr uint32_t kMaxOutputs = DECOMPRESS_CS_MAX_OUTPUTS;

struct PipelineCache {
	VkDescriptorSetLayout set0 = VK_NULL_HANDLE;
	VkDescriptorSetLayout set1 = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	std::unordered_map<const void *, VkPipeline> pipelines;

	void destroy(NtcVkDevice &dev) {
		for (auto &kv : pipelines) {
			if (kv.second) {
				dev.vk.DestroyPipeline(dev.device, kv.second, nullptr);
			}
		}
		pipelines.clear();
		if (layout) {
			dev.vk.DestroyPipelineLayout(dev.device, layout, nullptr);
			layout = VK_NULL_HANDLE;
		}
		if (set0) {
			dev.vk.DestroyDescriptorSetLayout(dev.device, set0, nullptr);
			set0 = VK_NULL_HANDLE;
		}
		if (set1) {
			dev.vk.DestroyDescriptorSetLayout(dev.device, set1, nullptr);
			set1 = VK_NULL_HANDLE;
		}
	}
};

bool read_stream_range(ntc::IStream *stream, const ntc::BufferFootprint &fp, ntc::IContext *ctx,
		std::vector<uint8_t> &out, std::string &err) {
	std::vector<uint8_t> stored(size_t(fp.rangeInStream.size));
	if (!stream->Seek(fp.rangeInStream.offset) || !stream->Read(stored.data(), stored.size())) {
		err = "Failed to read NTC stream range.";
		return false;
	}
	if (fp.compressionType == ntc::CompressionType::None) {
		out = std::move(stored);
		return true;
	}
	out.resize(size_t(fp.uncompressedSize));
	ntc::Status st = ctx->DecompressBuffer(fp.compressionType, stored.data(), stored.size(), out.data(), out.size(),
			fp.uncompressedCrc32);
	if (st != ntc::Status::Ok) {
		err = std::string("GDeflate decompress failed: ") + ntc::StatusToString(st) + " " + ntc::GetLastErrorMessage();
		return false;
	}
	return true;
}

Image::Format godot_format_for_bc(ntc::BlockCompressedFormat fmt) {
	switch (fmt) {
		case ntc::BlockCompressedFormat::BC1:
			return Image::FORMAT_DXT1;
		case ntc::BlockCompressedFormat::BC2:
			return Image::FORMAT_DXT3;
		case ntc::BlockCompressedFormat::BC3:
			return Image::FORMAT_DXT5;
		case ntc::BlockCompressedFormat::BC4:
			return Image::FORMAT_RGTC_R;
		case ntc::BlockCompressedFormat::BC5:
			return Image::FORMAT_RGTC_RG;
		case ntc::BlockCompressedFormat::BC6:
			return Image::FORMAT_BPTC_RGBFU;
		case ntc::BlockCompressedFormat::BC7:
			return Image::FORMAT_BPTC_RGBA;
		default:
			return Image::FORMAT_RGBA8;
	}
}

uint32_t bc_block_bytes(ntc::BlockCompressedFormat fmt) {
	switch (fmt) {
		case ntc::BlockCompressedFormat::BC1:
		case ntc::BlockCompressedFormat::BC4:
			return 8;
		default:
			return 16;
	}
}

bool is_small_bc(ntc::BlockCompressedFormat fmt) {
	return fmt == ntc::BlockCompressedFormat::BC1 || fmt == ntc::BlockCompressedFormat::BC4;
}

bool init_decomp_layouts(NtcVkDevice &dev, PipelineCache &cache, std::string &err) {
	if (cache.layout) {
		return true;
	}

	VkDescriptorSetLayoutBinding b0[4]{};
	b0[0].binding = NTC_BINDING_DECOMPRESSION_CONSTANT_BUFFER;
	b0[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	b0[0].descriptorCount = 1;
	b0[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	b0[1].binding = NTC_BINDING_DECOMPRESSION_LATENT_TEXTURE;
	b0[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	b0[1].descriptorCount = 1;
	b0[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	b0[2].binding = NTC_BINDING_DECOMPRESSION_WEIGHT_BUFFER;
	b0[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	b0[2].descriptorCount = 1;
	b0[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	b0[3].binding = NTC_BINDING_DECOMPRESSION_LATENT_SAMPLER;
	b0[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	b0[3].descriptorCount = 1;
	b0[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo l0{};
	l0.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	l0.bindingCount = 4;
	l0.pBindings = b0;
	if (dev.vk.CreateDescriptorSetLayout(dev.device, &l0, nullptr, &cache.set0) != VK_SUCCESS) {
		err = "CreateDescriptorSetLayout set0 failed.";
		return false;
	}

	VkDescriptorSetLayoutBinding b1{};
	b1.binding = NTC_BINDING_DECOMPRESSION_OUTPUT_TEXTURES;
	b1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	b1.descriptorCount = kMaxOutputs;
	b1.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo l1{};
	l1.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	l1.bindingCount = 1;
	l1.pBindings = &b1;
	if (dev.vk.CreateDescriptorSetLayout(dev.device, &l1, nullptr, &cache.set1) != VK_SUCCESS) {
		err = "CreateDescriptorSetLayout set1 failed.";
		return false;
	}

	VkDescriptorSetLayout sets[2] = { cache.set0, cache.set1 };
	VkPipelineLayoutCreateInfo pli{};
	pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pli.setLayoutCount = 2;
	pli.pSetLayouts = sets;
	if (dev.vk.CreatePipelineLayout(dev.device, &pli, nullptr, &cache.layout) != VK_SUCCESS) {
		err = "CreatePipelineLayout (decompress) failed.";
		return false;
	}
	return true;
}

bool init_bc_layouts(NtcVkDevice &dev, VkDescriptorSetLayout &set, VkPipelineLayout &layout, std::string &err) {
	VkDescriptorSetLayoutBinding b[4]{};
	b[0].binding = NTC_BINDING_BC_CONSTANT_BUFFER;
	b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	b[0].descriptorCount = 1;
	b[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	b[1].binding = NTC_BINDING_BC_INPUT_TEXTURE;
	b[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	b[1].descriptorCount = 1;
	b[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	b[2].binding = NTC_BINDING_BC_MODE_BUFFER;
	b[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	b[2].descriptorCount = 1;
	b[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	b[3].binding = NTC_BINDING_BC_OUTPUT_TEXTURE;
	b[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	b[3].descriptorCount = 1;
	b[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo li{};
	li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	li.bindingCount = 4;
	li.pBindings = b;
	if (dev.vk.CreateDescriptorSetLayout(dev.device, &li, nullptr, &set) != VK_SUCCESS) {
		err = "CreateDescriptorSetLayout (BC) failed.";
		return false;
	}
	VkPipelineLayoutCreateInfo pli{};
	pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pli.setLayoutCount = 1;
	pli.pSetLayouts = &set;
	if (dev.vk.CreatePipelineLayout(dev.device, &pli, nullptr, &layout) != VK_SUCCESS) {
		err = "CreatePipelineLayout (BC) failed.";
		return false;
	}
	return true;
}

VkPipeline get_or_create_pipeline(NtcVkDevice &dev, std::unordered_map<const void *, VkPipeline> &cache,
		VkPipelineLayout layout, const ntc::ComputePassDesc &pass, std::string &err) {
	auto it = cache.find(pass.computeShader);
	if (it != cache.end()) {
		return it->second;
	}
	if (!pass.computeShader || pass.computeShaderSize == 0) {
		err = "ComputePassDesc has no shader bytecode.";
		return VK_NULL_HANDLE;
	}
	VkShaderModule mod = dev.create_shader(pass.computeShader, pass.computeShaderSize, err);
	if (!mod) {
		return VK_NULL_HANDLE;
	}
	VkComputePipelineCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	ci.stage.module = mod;
	ci.stage.pName = "main";
	ci.layout = layout;
	VkPipeline pipe = VK_NULL_HANDLE;
	VkResult r = dev.vk.CreateComputePipelines(dev.device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipe);
	dev.vk.DestroyShaderModule(dev.device, mod, nullptr);
	if (r != VK_SUCCESS) {
		err = "vkCreateComputePipelines failed.";
		return VK_NULL_HANDLE;
	}
	cache[pass.computeShader] = pipe;
	return pipe;
}

struct TexWork {
	ntc::ITextureMetadata *meta = nullptr;
	NtcDecodedTexture out;
	ntc::BlockCompressedFormat bc = ntc::BlockCompressedFormat::None;
	NtcVkImage color{};
	NtcVkImage blocks{};
	std::vector<VkImageView> color_mips;
	std::vector<VkImageView> block_mips;
	NtcVkBuffer mode_buf{};
};

} // namespace

bool ntc_decode_file_a1(NtcVkDevice &dev, ntc::IContext *ctx, const uint8_t *file_bytes, size_t file_size,
		NtcDecodeResult &out, std::string &err) {
	out = {};
	if (!dev.is_ready() || !ctx) {
		err = "NTC runtime is not initialized.";
		return false;
	}

	ntc::IStream *stream = nullptr;
	ntc::Status st = ctx->OpenReadOnlyMemory(file_bytes, file_size, &stream);
	if (st != ntc::Status::Ok) {
		err = std::string("OpenReadOnlyMemory: ") + ntc::GetLastErrorMessage();
		return false;
	}

	ntc::ITextureSetMetadata *meta = nullptr;
	st = ctx->CreateTextureSetMetadataFromStream(stream, &meta);
	if (st != ntc::Status::Ok) {
		ctx->CloseMemory(stream);
		err = std::string("CreateTextureSetMetadataFromStream: ") + ntc::GetLastErrorMessage();
		return false;
	}

	const ntc::TextureSetDesc &desc = meta->GetDesc();
	out.width = desc.width;
	out.height = desc.height;
	out.mips = desc.mips;
	out.channels = desc.channels;
	out.bits_per_pixel = ntc::GetLatentShapeBitsPerPixel(meta->GetLatentShape());

	ntc::InferenceWeightType weight_type = ntc::InferenceWeightType::GenericInt8;
	if (!meta->IsInferenceWeightTypeSupported(weight_type)) {
		weight_type = meta->GetBestSupportedWeightType();
	}
	if (weight_type == ntc::InferenceWeightType::Unknown) {
		err = "No supported inference weight type in this .ntc file.";
		ctx->DestroyTextureSetMetadata(meta);
		ctx->CloseMemory(stream);
		return false;
	}
	out.weight_type = ntc::InferenceWeightTypeToString(weight_type);

	const void *weight_data = nullptr;
	size_t weight_upload = 0;
	size_t weight_converted = 0;
	st = meta->GetInferenceWeights(weight_type, &weight_data, &weight_upload, &weight_converted);
	if (st != ntc::Status::Ok) {
		err = std::string("GetInferenceWeights: ") + ntc::GetLastErrorMessage();
		ctx->DestroyTextureSetMetadata(meta);
		ctx->CloseMemory(stream);
		return false;
	}

	ntc::LatentTextureDesc latent_desc = meta->GetLatentTextureDesc();
	NtcVkImage latent{};
	if (!dev.create_image(uint32_t(latent_desc.width), uint32_t(latent_desc.height), uint32_t(latent_desc.arraySize),
				uint32_t(latent_desc.mipLevels), dev.latent_format,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, latent, err, 0, &dev.latent_swizzle)) {
		ctx->DestroyTextureSetMetadata(meta);
		ctx->CloseMemory(stream);
		return false;
	}

	size_t staging_need = weight_upload + 4096;
	for (int mip = 0; mip < latent_desc.mipLevels; ++mip) {
		for (int layer = 0; layer < latent_desc.arraySize; ++layer) {
			ntc::LatentTextureFootprint fp{};
			if (meta->GetLatentTextureFootprint(mip, layer, fp) == ntc::Status::Ok) {
				staging_need += size_t(fp.buffer.uncompressedSize) + 256;
			}
		}
	}
	staging_need = std::max(staging_need, size_t(desc.width) * size_t(desc.height) * 16 + 1024);

	NtcVkBuffer staging{}, weight_gpu{}, dummy_mode{};
	if (!dev.create_buffer(staging_need, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, err)) {
		dev.destroy_image(latent);
		ctx->DestroyTextureSetMetadata(meta);
		ctx->CloseMemory(stream);
		return false;
	}
	if (!dev.create_buffer(std::max(weight_upload, size_t(256)),
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, weight_gpu, err)) {
		dev.destroy_buffer(staging);
		dev.destroy_image(latent);
		ctx->DestroyTextureSetMetadata(meta);
		ctx->CloseMemory(stream);
		return false;
	}
	if (!dev.create_buffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, dummy_mode,
				err)) {
		dev.destroy_buffer(weight_gpu);
		dev.destroy_buffer(staging);
		dev.destroy_image(latent);
		ctx->DestroyTextureSetMetadata(meta);
		ctx->CloseMemory(stream);
		return false;
	}

	NtcVkImage dummy{};
	if (!dev.create_image(4, 4, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, dummy, err)) {
		dev.destroy_buffer(dummy_mode);
		dev.destroy_buffer(weight_gpu);
		dev.destroy_buffer(staging);
		dev.destroy_image(latent);
		ctx->DestroyTextureSetMetadata(meta);
		ctx->CloseMemory(stream);
		return false;
	}

	std::vector<TexWork> works;
	works.reserve(size_t(meta->GetTextureCount()));
	for (int i = 0; i < meta->GetTextureCount(); ++i) {
		ntc::ITextureMetadata *tm = meta->GetTexture(i);
		TexWork w;
		w.meta = tm;
		w.out.name = tm->GetName() ? tm->GetName() : ("texture_" + std::to_string(i)).c_str();
		w.out.first_channel = tm->GetFirstChannel();
		w.out.num_channels = tm->GetNumChannels();
		w.out.width = desc.width;
		w.out.height = desc.height;
		w.out.mips = desc.mips;
		w.out.srgb = tm->GetRgbColorSpace() == ntc::ColorSpace::sRGB;
		w.bc = tm->GetBlockCompressedFormat();
		if (w.bc == ntc::BlockCompressedFormat::None) {
			w.bc = (w.out.num_channels <= 1) ? ntc::BlockCompressedFormat::BC4 : ntc::BlockCompressedFormat::BC7;
		}
		w.out.image_format = godot_format_for_bc(w.bc);

		const VkFormat color_fmt = (w.out.num_channels <= 1) ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
		if (!dev.create_image(uint32_t(desc.width), uint32_t(desc.height), 1, uint32_t(desc.mips), color_fmt,
					VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, w.color,
					err)) {
			break;
		}
		const VkFormat block_fmt = is_small_bc(w.bc) ? VK_FORMAT_R32G32_UINT : VK_FORMAT_R32G32B32A32_UINT;
		if (!dev.create_image(uint32_t((desc.width + 3) / 4), uint32_t((desc.height + 3) / 4), 1, uint32_t(desc.mips),
					block_fmt, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, w.blocks, err)) {
			break;
		}
		w.color_mips.resize(size_t(desc.mips));
		w.block_mips.resize(size_t(desc.mips));
		bool views_ok = true;
		for (int m = 0; m < desc.mips && views_ok; ++m) {
			views_ok = dev.create_image_view(w.color, uint32_t(m), 1, 0, 1, w.color_mips[m], err) &&
					dev.create_image_view(w.blocks, uint32_t(m), 1, 0, 1, w.block_mips[m], err);
		}
		if (!views_ok) {
			break;
		}
		works.push_back(std::move(w));
	}
	if (works.size() != size_t(meta->GetTextureCount())) {
		// cleanup below
	}

	auto cleanup = [&]() {
		for (auto &w : works) {
			for (VkImageView v : w.color_mips) {
				if (v) {
					dev.vk.DestroyImageView(dev.device, v, nullptr);
				}
			}
			for (VkImageView v : w.block_mips) {
				if (v) {
					dev.vk.DestroyImageView(dev.device, v, nullptr);
				}
			}
			dev.destroy_image(w.color);
			dev.destroy_image(w.blocks);
			dev.destroy_buffer(w.mode_buf);
		}
		dev.destroy_image(dummy);
		dev.destroy_image(latent);
		dev.destroy_buffer(dummy_mode);
		dev.destroy_buffer(weight_gpu);
		dev.destroy_buffer(staging);
		ctx->DestroyTextureSetMetadata(meta);
		ctx->CloseMemory(stream);
	};

	if (works.size() != size_t(meta->GetTextureCount())) {
		cleanup();
		if (err.empty()) {
			err = "Failed to allocate per-texture GPU images.";
		}
		return false;
	}

	if (!dev.begin_commands(err)) {
		cleanup();
		return false;
	}

	dev.barrier(latent.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			latent.layers, latent.mips);
	dev.barrier(dummy.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	size_t pack = 0;
	auto align256 = [](size_t v) { return (v + 255u) & ~size_t(255); };
	for (int mip = 0; mip < latent_desc.mipLevels; ++mip) {
		for (int layer = 0; layer < latent_desc.arraySize; ++layer) {
			ntc::LatentTextureFootprint fp{};
			if (meta->GetLatentTextureFootprint(mip, layer, fp) != ntc::Status::Ok) {
				continue;
			}
			std::vector<uint8_t> pixels;
			if (!read_stream_range(stream, fp.buffer, ctx, pixels, err)) {
				dev.vk.EndCommandBuffer(dev.cmd);
				cleanup();
				return false;
			}
			pack = align256(pack);
			if (pack + pixels.size() > staging.size) {
				err = "Latent slice exceeds staging buffer.";
				dev.vk.EndCommandBuffer(dev.cmd);
				cleanup();
				return false;
			}
			std::memcpy(static_cast<uint8_t *>(staging.mapped) + pack, pixels.data(), pixels.size());
			VkBufferImageCopy copy{};
			copy.bufferOffset = pack;
			copy.bufferRowLength = fp.rowPitch ? uint32_t(fp.rowPitch / 2) : uint32_t(fp.width);
			copy.bufferImageHeight = uint32_t(fp.height);
			copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy.imageSubresource.mipLevel = uint32_t(mip);
			copy.imageSubresource.baseArrayLayer = uint32_t(layer);
			copy.imageSubresource.layerCount = 1;
			copy.imageExtent = { uint32_t(fp.width), uint32_t(fp.height), 1 };
			dev.vk.CmdCopyBufferToImage(dev.cmd, staging.buffer, latent.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
					&copy);
			pack += pixels.size();
		}
	}

	pack = align256(pack);
	if (pack + weight_upload > staging.size) {
		err = "Weight upload exceeds staging buffer.";
		dev.vk.EndCommandBuffer(dev.cmd);
		cleanup();
		return false;
	}
	std::memcpy(static_cast<uint8_t *>(staging.mapped) + pack, weight_data, weight_upload);
	VkBufferCopy wcopy{ pack, 0, weight_upload };
	dev.vk.CmdCopyBuffer(dev.cmd, staging.buffer, weight_gpu.buffer, 1, &wcopy);
	dev.buffer_barrier(weight_gpu.buffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, weight_gpu.size);

	dev.barrier(latent.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, latent.layers, latent.mips);

	for (auto &w : works) {
		dev.barrier(w.color.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
				VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 1,
				w.color.mips);
		dev.barrier(w.blocks.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
				VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 1,
				w.blocks.mips);
	}

	if (!dev.submit_and_wait(err)) {
		cleanup();
		return false;
	}

	PipelineCache decomp;
	VkDescriptorSetLayout bc_set_layout = VK_NULL_HANDLE;
	VkPipelineLayout bc_layout = VK_NULL_HANDLE;
	std::unordered_map<const void *, VkPipeline> bc_pipelines;
	if (!init_decomp_layouts(dev, decomp, err) || !init_bc_layouts(dev, bc_set_layout, bc_layout, err)) {
		decomp.destroy(dev);
		if (bc_layout) {
			dev.vk.DestroyPipelineLayout(dev.device, bc_layout, nullptr);
		}
		if (bc_set_layout) {
			dev.vk.DestroyDescriptorSetLayout(dev.device, bc_set_layout, nullptr);
		}
		cleanup();
		return false;
	}

	VkDescriptorPoolSize pool_sizes[] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64 },
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 16 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256 },
	};
	VkDescriptorPoolCreateInfo dpi{};
	dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpi.maxSets = 128;
	dpi.poolSizeCount = 5;
	dpi.pPoolSizes = pool_sizes;
	VkDescriptorPool pool = VK_NULL_HANDLE;
	dev.vk.CreateDescriptorPool(dev.device, &dpi, nullptr, &pool);

	NtcVkBuffer ubo{};
	dev.create_buffer(ntc::MaxComputePassConstantSize * 128,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ubo, err);
	size_t ubo_cursor = 0;

	auto alloc_set = [&](VkDescriptorSetLayout layout) {
		VkDescriptorSetAllocateInfo ai{};
		ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		ai.descriptorPool = pool;
		ai.descriptorSetCount = 1;
		ai.pSetLayouts = &layout;
		VkDescriptorSet set = VK_NULL_HANDLE;
		dev.vk.AllocateDescriptorSets(dev.device, &ai, &set);
		return set;
	};

	if (!dev.begin_commands(err)) {
		dev.destroy_buffer(ubo);
		dev.vk.DestroyDescriptorPool(dev.device, pool, nullptr);
		decomp.destroy(dev);
		for (auto &kv : bc_pipelines) {
			dev.vk.DestroyPipeline(dev.device, kv.second, nullptr);
		}
		dev.vk.DestroyPipelineLayout(dev.device, bc_layout, nullptr);
		dev.vk.DestroyDescriptorSetLayout(dev.device, bc_set_layout, nullptr);
		cleanup();
		return false;
	}

	for (int mip = 0; mip < desc.mips; ++mip) {
		std::vector<ntc::OutputTextureDesc> outputs(works.size());
		for (size_t i = 0; i < works.size(); ++i) {
			outputs[i].descriptorIndex = int(i);
			outputs[i].firstChannel = works[i].out.first_channel;
			outputs[i].numChannels = works[i].out.num_channels;
			outputs[i].rgbColorSpace = works[i].out.srgb ? ntc::ColorSpace::sRGB : ntc::ColorSpace::Linear;
			outputs[i].alphaColorSpace = ntc::ColorSpace::Linear;
			outputs[i].ditherScale = 1.f / 255.f;
			outputs[i].quantizationScale = 1.f / 255.f;
		}

		ntc::MakeDecompressionComputePassParameters dp{};
		dp.textureSetMetadata = meta;
		dp.weightType = weight_type;
		dp.mipLevel = mip;
		dp.pOutputTextures = outputs.data();
		dp.numOutputTextures = int(outputs.size());
		ntc::ComputePassDesc pass{};
		st = ctx->MakeDecompressionComputePass(dp, &pass);
		if (st != ntc::Status::Ok && st != ntc::Status::ShaderUnavailable) {
			err = std::string("MakeDecompressionComputePass: ") + ntc::GetLastErrorMessage();
			dev.vk.EndCommandBuffer(dev.cmd);
			goto fail_gpu;
		}
		if (!pass.computeShader) {
			err = "LibNTC returned no decompression SPIR-V (ShaderUnavailable).";
			dev.vk.EndCommandBuffer(dev.cmd);
			goto fail_gpu;
		}

		VkPipeline pipe = get_or_create_pipeline(dev, decomp.pipelines, decomp.layout, pass, err);
		if (!pipe) {
			dev.vk.EndCommandBuffer(dev.cmd);
			goto fail_gpu;
		}

		const size_t ubo_off = ubo_cursor;
		std::memcpy(static_cast<uint8_t *>(ubo.mapped) + ubo_off, pass.constantBufferData, pass.constantBufferSize);
		ubo_cursor += (pass.constantBufferSize + 255u) & ~255u;

		VkDescriptorSet set0 = alloc_set(decomp.set0);
		VkDescriptorSet set1 = alloc_set(decomp.set1);
		VkDescriptorBufferInfo ubo_info{ ubo.buffer, ubo_off, pass.constantBufferSize };
		VkDescriptorImageInfo latent_info{};
		latent_info.imageView = latent.view;
		latent_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		VkDescriptorBufferInfo weight_info{ weight_gpu.buffer, 0, VK_WHOLE_SIZE };
		VkDescriptorImageInfo samp_info{};
		samp_info.sampler = dev.latent_sampler;
		VkWriteDescriptorSet writes[4]{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = set0;
		writes[0].dstBinding = NTC_BINDING_DECOMPRESSION_CONSTANT_BUFFER;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[0].pBufferInfo = &ubo_info;
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = set0;
		writes[1].dstBinding = NTC_BINDING_DECOMPRESSION_LATENT_TEXTURE;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		writes[1].pImageInfo = &latent_info;
		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstSet = set0;
		writes[2].dstBinding = NTC_BINDING_DECOMPRESSION_WEIGHT_BUFFER;
		writes[2].descriptorCount = 1;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[2].pBufferInfo = &weight_info;
		writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[3].dstSet = set0;
		writes[3].dstBinding = NTC_BINDING_DECOMPRESSION_LATENT_SAMPLER;
		writes[3].descriptorCount = 1;
		writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		writes[3].pImageInfo = &samp_info;
		dev.vk.UpdateDescriptorSets(dev.device, 4, writes, 0, nullptr);

		VkDescriptorImageInfo out_infos[kMaxOutputs]{};
		for (uint32_t i = 0; i < kMaxOutputs; ++i) {
			out_infos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			if (i < works.size()) {
				out_infos[i].imageView = works[i].color_mips[mip];
			} else {
				out_infos[i].imageView = dummy.view;
			}
		}
		VkWriteDescriptorSet wout{};
		wout.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		wout.dstSet = set1;
		wout.dstBinding = NTC_BINDING_DECOMPRESSION_OUTPUT_TEXTURES;
		wout.descriptorCount = kMaxOutputs;
		wout.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		wout.pImageInfo = out_infos;
		dev.vk.UpdateDescriptorSets(dev.device, 1, &wout, 0, nullptr);

		VkDescriptorSet sets[2] = { set0, set1 };
		dev.vk.CmdBindPipeline(dev.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
		dev.vk.CmdBindDescriptorSets(dev.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, decomp.layout, 0, 2, sets, 0, nullptr);
		dev.vk.CmdDispatch(dev.cmd, uint32_t(pass.dispatchWidth), uint32_t(pass.dispatchHeight), 1);
	}

	for (auto &w : works) {
		dev.barrier(w.color.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 1, w.color.mips);
	}

	for (auto &w : works) {
		for (int mip = 0; mip < desc.mips; ++mip) {
			const int mip_w = std::max(desc.width >> mip, 1);
			const int mip_h = std::max(desc.height >> mip, 1);
			ntc::MakeBlockCompressionComputePassParameters bp{};
			bp.srcRect.width = mip_w;
			bp.srcRect.height = mip_h;
			bp.dstFormat = w.bc;
			bp.alphaThreshold = 1.f / 255.f;
			ntc::ComputePassDesc pass{};
			st = ctx->MakeBlockCompressionComputePass(bp, &pass);
			if (st != ntc::Status::Ok && st != ntc::Status::ShaderUnavailable) {
				err = std::string("MakeBlockCompressionComputePass: ") + ntc::GetLastErrorMessage();
				dev.vk.EndCommandBuffer(dev.cmd);
				goto fail_gpu;
			}
			VkPipeline pipe = get_or_create_pipeline(dev, bc_pipelines, bc_layout, pass, err);
			if (!pipe) {
				dev.vk.EndCommandBuffer(dev.cmd);
				goto fail_gpu;
			}
			const size_t ubo_off = ubo_cursor;
			std::memcpy(static_cast<uint8_t *>(ubo.mapped) + ubo_off, pass.constantBufferData, pass.constantBufferSize);
			ubo_cursor += (pass.constantBufferSize + 255u) & ~255u;

			VkDescriptorSet set = alloc_set(bc_set_layout);
			VkDescriptorBufferInfo ubo_info{ ubo.buffer, ubo_off, pass.constantBufferSize };
			VkDescriptorImageInfo in_info{};
			in_info.imageView = w.color_mips[mip];
			in_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VkDescriptorBufferInfo mode_info{ dummy_mode.buffer, 0, VK_WHOLE_SIZE };
			VkDescriptorImageInfo out_info{};
			out_info.imageView = w.block_mips[mip];
			out_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			VkWriteDescriptorSet wr[4]{};
			wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			wr[0].dstSet = set;
			wr[0].dstBinding = 0;
			wr[0].descriptorCount = 1;
			wr[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			wr[0].pBufferInfo = &ubo_info;
			wr[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			wr[1].dstSet = set;
			wr[1].dstBinding = 1;
			wr[1].descriptorCount = 1;
			wr[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			wr[1].pImageInfo = &in_info;
			wr[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			wr[2].dstSet = set;
			wr[2].dstBinding = 2;
			wr[2].descriptorCount = 1;
			wr[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			wr[2].pBufferInfo = &mode_info;
			wr[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			wr[3].dstSet = set;
			wr[3].dstBinding = 3;
			wr[3].descriptorCount = 1;
			wr[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			wr[3].pImageInfo = &out_info;
			dev.vk.UpdateDescriptorSets(dev.device, 4, wr, 0, nullptr);
			dev.vk.CmdBindPipeline(dev.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
			dev.vk.CmdBindDescriptorSets(dev.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bc_layout, 0, 1, &set, 0, nullptr);
			dev.vk.CmdDispatch(dev.cmd, uint32_t(pass.dispatchWidth), uint32_t(pass.dispatchHeight), 1);
		}
	}

	for (auto &w : works) {
		dev.barrier(w.blocks.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, 1, w.blocks.mips);
	}

	if (!dev.submit_and_wait(err)) {
		goto fail_gpu;
	}

	for (auto &w : works) {
		godot::PackedByteArray packed;
		for (int mip = 0; mip < desc.mips; ++mip) {
			const int mip_w = std::max(desc.width >> mip, 1);
			const int mip_h = std::max(desc.height >> mip, 1);
			const int bw = (mip_w + 3) / 4;
			const int bh = (mip_h + 3) / 4;
			const uint32_t row_bytes = uint32_t(bw) * bc_block_bytes(w.bc);
			const size_t mip_bytes = size_t(row_bytes) * size_t(bh);
			if (!dev.begin_commands(err)) {
				goto fail_gpu;
			}
			VkBufferImageCopy copy{};
			copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy.imageSubresource.mipLevel = uint32_t(mip);
			copy.imageSubresource.layerCount = 1;
			copy.imageExtent = { uint32_t(bw), uint32_t(bh), 1 };
			dev.vk.CmdCopyImageToBuffer(dev.cmd, w.blocks.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1,
					&copy);
			if (!dev.submit_and_wait(err)) {
				goto fail_gpu;
			}
			const int old_n = packed.size();
			packed.resize(old_n + int64_t(mip_bytes));
			std::memcpy(packed.ptrw() + old_n, staging.mapped, mip_bytes);
		}
		w.out.data = packed;
		out.textures.push_back(w.out);
	}

	dev.destroy_buffer(ubo);
	dev.vk.DestroyDescriptorPool(dev.device, pool, nullptr);
	decomp.destroy(dev);
	for (auto &kv : bc_pipelines) {
		dev.vk.DestroyPipeline(dev.device, kv.second, nullptr);
	}
	dev.vk.DestroyPipelineLayout(dev.device, bc_layout, nullptr);
	dev.vk.DestroyDescriptorSetLayout(dev.device, bc_set_layout, nullptr);
	cleanup();
	return true;

fail_gpu:
	dev.destroy_buffer(ubo);
	if (pool) {
		dev.vk.DestroyDescriptorPool(dev.device, pool, nullptr);
	}
	decomp.destroy(dev);
	for (auto &kv : bc_pipelines) {
		dev.vk.DestroyPipeline(dev.device, kv.second, nullptr);
	}
	if (bc_layout) {
		dev.vk.DestroyPipelineLayout(dev.device, bc_layout, nullptr);
	}
	if (bc_set_layout) {
		dev.vk.DestroyDescriptorSetLayout(dev.device, bc_set_layout, nullptr);
	}
	cleanup();
	return false;
}
