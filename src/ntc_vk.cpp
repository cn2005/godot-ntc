#include "ntc_vk.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstring>

namespace {
const char *kCoopVecExt = "VK_NV_cooperative_vector";
const char *k4444Ext = "VK_EXT_4444_formats";

template <typename T>
T load_inst(HMODULE lib, const char *name) {
	return reinterpret_cast<T>(GetProcAddress(lib, name));
}
} // namespace

NtcVkDevice::~NtcVkDevice() {
	shutdown();
}

bool NtcVkDevice::load_loader(std::string &out_error) {
	HMODULE lib = LoadLibraryA("vulkan-1.dll");
	if (!lib) {
		out_error = "vulkan-1.dll not found. Install a Vulkan-capable GPU driver.";
		return false;
	}
	loader_lib = lib;
	vk.GetInstanceProcAddr = load_inst<PFN_vkGetInstanceProcAddr>(lib, "vkGetInstanceProcAddr");
	vk.CreateInstance = load_inst<PFN_vkCreateInstance>(lib, "vkCreateInstance");
	vk.EnumerateInstanceVersion = load_inst<PFN_vkEnumerateInstanceVersion>(lib, "vkEnumerateInstanceVersion");
	vk.EnumerateInstanceExtensionProperties =
			load_inst<PFN_vkEnumerateInstanceExtensionProperties>(lib, "vkEnumerateInstanceExtensionProperties");
	if (!vk.GetInstanceProcAddr || !vk.CreateInstance) {
		out_error = "Failed to resolve Vulkan loader entry points.";
		return false;
	}
	return true;
}

void NtcVkDevice::load_instance_table() {
	auto gi = [&](const char *name) { return vk.GetInstanceProcAddr(instance, name); };
	vk.DestroyInstance = (PFN_vkDestroyInstance)gi("vkDestroyInstance");
	vk.EnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)gi("vkEnumeratePhysicalDevices");
	vk.GetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)gi("vkGetPhysicalDeviceProperties");
	vk.GetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)gi("vkGetPhysicalDeviceFeatures");
	vk.GetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)gi("vkGetPhysicalDeviceFeatures2");
	vk.GetPhysicalDeviceQueueFamilyProperties =
			(PFN_vkGetPhysicalDeviceQueueFamilyProperties)gi("vkGetPhysicalDeviceQueueFamilyProperties");
	vk.GetPhysicalDeviceMemoryProperties =
			(PFN_vkGetPhysicalDeviceMemoryProperties)gi("vkGetPhysicalDeviceMemoryProperties");
	vk.GetPhysicalDeviceFormatProperties =
			(PFN_vkGetPhysicalDeviceFormatProperties)gi("vkGetPhysicalDeviceFormatProperties");
	vk.EnumerateDeviceExtensionProperties =
			(PFN_vkEnumerateDeviceExtensionProperties)gi("vkEnumerateDeviceExtensionProperties");
	vk.CreateDevice = (PFN_vkCreateDevice)gi("vkCreateDevice");
	vk.GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)gi("vkGetDeviceProcAddr");
}

void NtcVkDevice::load_device_table() {
	auto gd = [&](const char *name) { return vk.GetDeviceProcAddr(device, name); };
	vk.DestroyDevice = (PFN_vkDestroyDevice)gd("vkDestroyDevice");
	vk.GetDeviceQueue = (PFN_vkGetDeviceQueue)gd("vkGetDeviceQueue");
	vk.CreateCommandPool = (PFN_vkCreateCommandPool)gd("vkCreateCommandPool");
	vk.DestroyCommandPool = (PFN_vkDestroyCommandPool)gd("vkDestroyCommandPool");
	vk.AllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)gd("vkAllocateCommandBuffers");
	vk.FreeCommandBuffers = (PFN_vkFreeCommandBuffers)gd("vkFreeCommandBuffers");
	vk.BeginCommandBuffer = (PFN_vkBeginCommandBuffer)gd("vkBeginCommandBuffer");
	vk.EndCommandBuffer = (PFN_vkEndCommandBuffer)gd("vkEndCommandBuffer");
	vk.ResetCommandBuffer = (PFN_vkResetCommandBuffer)gd("vkResetCommandBuffer");
	vk.QueueSubmit = (PFN_vkQueueSubmit)gd("vkQueueSubmit");
	vk.QueueWaitIdle = (PFN_vkQueueWaitIdle)gd("vkQueueWaitIdle");
	vk.DeviceWaitIdle = (PFN_vkDeviceWaitIdle)gd("vkDeviceWaitIdle");
	vk.CreateFence = (PFN_vkCreateFence)gd("vkCreateFence");
	vk.DestroyFence = (PFN_vkDestroyFence)gd("vkDestroyFence");
	vk.WaitForFences = (PFN_vkWaitForFences)gd("vkWaitForFences");
	vk.ResetFences = (PFN_vkResetFences)gd("vkResetFences");
	vk.CreateBuffer = (PFN_vkCreateBuffer)gd("vkCreateBuffer");
	vk.DestroyBuffer = (PFN_vkDestroyBuffer)gd("vkDestroyBuffer");
	vk.GetBufferMemoryRequirements = (PFN_vkGetBufferMemoryRequirements)gd("vkGetBufferMemoryRequirements");
	vk.AllocateMemory = (PFN_vkAllocateMemory)gd("vkAllocateMemory");
	vk.FreeMemory = (PFN_vkFreeMemory)gd("vkFreeMemory");
	vk.BindBufferMemory = (PFN_vkBindBufferMemory)gd("vkBindBufferMemory");
	vk.MapMemory = (PFN_vkMapMemory)gd("vkMapMemory");
	vk.UnmapMemory = (PFN_vkUnmapMemory)gd("vkUnmapMemory");
	vk.CreateImage = (PFN_vkCreateImage)gd("vkCreateImage");
	vk.DestroyImage = (PFN_vkDestroyImage)gd("vkDestroyImage");
	vk.GetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)gd("vkGetImageMemoryRequirements");
	vk.BindImageMemory = (PFN_vkBindImageMemory)gd("vkBindImageMemory");
	vk.CreateImageView = (PFN_vkCreateImageView)gd("vkCreateImageView");
	vk.DestroyImageView = (PFN_vkDestroyImageView)gd("vkDestroyImageView");
	vk.CreateSampler = (PFN_vkCreateSampler)gd("vkCreateSampler");
	vk.DestroySampler = (PFN_vkDestroySampler)gd("vkDestroySampler");
	vk.CreateShaderModule = (PFN_vkCreateShaderModule)gd("vkCreateShaderModule");
	vk.DestroyShaderModule = (PFN_vkDestroyShaderModule)gd("vkDestroyShaderModule");
	vk.CreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)gd("vkCreateDescriptorSetLayout");
	vk.DestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)gd("vkDestroyDescriptorSetLayout");
	vk.CreatePipelineLayout = (PFN_vkCreatePipelineLayout)gd("vkCreatePipelineLayout");
	vk.DestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)gd("vkDestroyPipelineLayout");
	vk.CreateComputePipelines = (PFN_vkCreateComputePipelines)gd("vkCreateComputePipelines");
	vk.DestroyPipeline = (PFN_vkDestroyPipeline)gd("vkDestroyPipeline");
	vk.CreateDescriptorPool = (PFN_vkCreateDescriptorPool)gd("vkCreateDescriptorPool");
	vk.DestroyDescriptorPool = (PFN_vkDestroyDescriptorPool)gd("vkDestroyDescriptorPool");
	vk.AllocateDescriptorSets = (PFN_vkAllocateDescriptorSets)gd("vkAllocateDescriptorSets");
	vk.UpdateDescriptorSets = (PFN_vkUpdateDescriptorSets)gd("vkUpdateDescriptorSets");
	vk.CmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)gd("vkCmdPipelineBarrier");
	vk.CmdBindPipeline = (PFN_vkCmdBindPipeline)gd("vkCmdBindPipeline");
	vk.CmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)gd("vkCmdBindDescriptorSets");
	vk.CmdDispatch = (PFN_vkCmdDispatch)gd("vkCmdDispatch");
	vk.CmdCopyBuffer = (PFN_vkCmdCopyBuffer)gd("vkCmdCopyBuffer");
	vk.CmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage)gd("vkCmdCopyBufferToImage");
	vk.CmdCopyImageToBuffer = (PFN_vkCmdCopyImageToBuffer)gd("vkCmdCopyImageToBuffer");
}

bool NtcVkDevice::create_instance(std::string &out_error) {
	uint32_t api_version = VK_API_VERSION_1_2;
	if (vk.EnumerateInstanceVersion) {
		vk.EnumerateInstanceVersion(&api_version);
	}
	if (api_version < VK_API_VERSION_1_2) {
		out_error = "Vulkan 1.2 or newer is required.";
		return false;
	}

	VkApplicationInfo app{};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "ntc_godot";
	app.applicationVersion = 1;
	app.pEngineName = "Godot";
	app.engineVersion = 1;
	app.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ci.pApplicationInfo = &app;
	if (vk.CreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) {
		app.apiVersion = VK_API_VERSION_1_2;
		if (vk.CreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) {
			out_error = "vkCreateInstance failed.";
			return false;
		}
	}
	owns_instance = true;
	return true;
}

bool NtcVkDevice::pick_device(std::string &out_error) {
	uint32_t count = 0;
	vk.EnumeratePhysicalDevices(instance, &count, nullptr);
	if (count == 0) {
		out_error = "No Vulkan physical devices.";
		return false;
	}
	std::vector<VkPhysicalDevice> devices(count);
	vk.EnumeratePhysicalDevices(instance, &count, devices.data());

	int best = -1;
	int best_score = -1;
	for (uint32_t i = 0; i < count; ++i) {
		VkPhysicalDeviceProperties props{};
		vk.GetPhysicalDeviceProperties(devices[i], &props);
		int score = 0;
		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			score += 1000;
		}
		if (props.vendorID == 0x10DE) {
			score += 100;
		}
		if (score > best_score) {
			best_score = score;
			best = int(i);
		}
	}
	physical_device = devices[best];
	vk.GetPhysicalDeviceProperties(physical_device, &gpu_props);
	vk.GetPhysicalDeviceMemoryProperties(physical_device, &memory_props);
	return true;
}

bool NtcVkDevice::create_logical_device(std::string &out_error) {
	uint32_t qcount = 0;
	vk.GetPhysicalDeviceQueueFamilyProperties(physical_device, &qcount, nullptr);
	std::vector<VkQueueFamilyProperties> qprops(qcount);
	vk.GetPhysicalDeviceQueueFamilyProperties(physical_device, &qcount, qprops.data());
	queue_family = UINT32_MAX;
	for (uint32_t i = 0; i < qcount; ++i) {
		if (qprops[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
			queue_family = i;
			break;
		}
	}
	if (queue_family == UINT32_MAX) {
		out_error = "No compute queue family.";
		return false;
	}

	uint32_t ext_count = 0;
	vk.EnumerateDeviceExtensionProperties(physical_device, nullptr, &ext_count, nullptr);
	std::vector<VkExtensionProperties> exts(ext_count);
	vk.EnumerateDeviceExtensionProperties(physical_device, nullptr, &ext_count, exts.data());
	auto has_ext = [&](const char *name) {
		for (const auto &e : exts) {
			if (std::strcmp(e.extensionName, name) == 0) {
				return true;
			}
		}
		return false;
	};

	std::vector<const char *> enable_exts;
	// A1 uses GenericInt8 (DP4a). Cooperative Vector is optional and needs a newer header.
	coop_vec_enabled = false;
	(void)kCoopVecExt;
	const bool has_4444 = has_ext(k4444Ext);
	if (has_4444) {
		enable_exts.push_back(k4444Ext);
	}

	VkPhysicalDeviceFeatures base_feats{};
	vk.GetPhysicalDeviceFeatures(physical_device, &base_feats);

	VkPhysicalDeviceFeatures2 feats2{};
	feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	VkPhysicalDeviceVulkan11Features vk11{};
	vk11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	VkPhysicalDeviceVulkan12Features vk12{};
	vk12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	VkPhysicalDeviceVulkan13Features vk13{};
	vk13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	feats2.pNext = &vk11;
	vk11.pNext = &vk12;
	vk12.pNext = &vk13;
	if (vk.GetPhysicalDeviceFeatures2) {
		vk.GetPhysicalDeviceFeatures2(physical_device, &feats2);
	}

	VkPhysicalDeviceFeatures enabled_base{};
	enabled_base.shaderInt16 = base_feats.shaderInt16;

	vk11.storageBuffer16BitAccess = vk11.storageBuffer16BitAccess;
	vk12.shaderFloat16 = vk12.shaderFloat16;
	vk12.storageBuffer8BitAccess = vk12.storageBuffer8BitAccess;
	vk12.shaderInt8 = vk12.shaderInt8;
	vk13.shaderDemoteToHelperInvocation = vk13.shaderDemoteToHelperInvocation;
	vk13.shaderIntegerDotProduct = vk13.shaderIntegerDotProduct;

	float prio = 1.f;
	VkDeviceQueueCreateInfo qci{};
	qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	qci.queueFamilyIndex = queue_family;
	qci.queueCount = 1;
	qci.pQueuePriorities = &prio;

	VkDeviceCreateInfo dci{};
	dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dci.pNext = &feats2;
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qci;
	dci.enabledExtensionCount = uint32_t(enable_exts.size());
	dci.ppEnabledExtensionNames = enable_exts.empty() ? nullptr : enable_exts.data();
	dci.pEnabledFeatures = &enabled_base;

	if (vk.CreateDevice(physical_device, &dci, nullptr, &device) != VK_SUCCESS) {
		out_error = "vkCreateDevice failed (NTC feature chain).";
		return false;
	}

	VkFormatProperties fp{};
	vk.GetPhysicalDeviceFormatProperties(physical_device, VK_FORMAT_A4R4G4B4_UNORM_PACK16, &fp);
	if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) {
		latent_format = VK_FORMAT_A4R4G4B4_UNORM_PACK16;
		latent_swizzle = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
			VK_COMPONENT_SWIZZLE_IDENTITY };
	} else {
		latent_format = VK_FORMAT_R4G4B4A4_UNORM_PACK16;
		// A4R4G4B4 bits A[15:12] R[11:8] G[7:4] B[3:0] vs R4G4B4A4 R[15:12] G[11:8] B[7:4] A[3:0]
		latent_swizzle = { VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_R };
	}
	return true;
}

bool NtcVkDevice::initialize(std::string &out_error) {
	if (!load_loader(out_error)) {
		return false;
	}
	if (!create_instance(out_error)) {
		return false;
	}
	load_instance_table();
	if (!pick_device(out_error)) {
		return false;
	}
	if (!create_logical_device(out_error)) {
		return false;
	}
	load_device_table();
	vk.GetDeviceQueue(device, queue_family, 0, &queue);

	VkCommandPoolCreateInfo pci{};
	pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pci.queueFamilyIndex = queue_family;
	if (vk.CreateCommandPool(device, &pci, nullptr, &command_pool) != VK_SUCCESS) {
		out_error = "vkCreateCommandPool failed.";
		return false;
	}

	VkCommandBufferAllocateInfo cai{};
	cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cai.commandPool = command_pool;
	cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cai.commandBufferCount = 1;
	if (vk.AllocateCommandBuffers(device, &cai, &cmd) != VK_SUCCESS) {
		out_error = "vkAllocateCommandBuffers failed.";
		return false;
	}

	VkSamplerCreateInfo sci{};
	sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sci.magFilter = VK_FILTER_LINEAR;
	sci.minFilter = VK_FILTER_LINEAR;
	sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	if (vk.CreateSampler(device, &sci, nullptr, &latent_sampler) != VK_SUCCESS) {
		out_error = "vkCreateSampler failed.";
		return false;
	}
	return true;
}

void NtcVkDevice::shutdown() {
	if (device && vk.DeviceWaitIdle) {
		vk.DeviceWaitIdle(device);
	}
	if (device && latent_sampler && vk.DestroySampler) {
		vk.DestroySampler(device, latent_sampler, nullptr);
		latent_sampler = VK_NULL_HANDLE;
	}
	if (device && command_pool && vk.DestroyCommandPool) {
		vk.DestroyCommandPool(device, command_pool, nullptr);
		command_pool = VK_NULL_HANDLE;
		cmd = VK_NULL_HANDLE;
	}
	if (device && vk.DestroyDevice) {
		vk.DestroyDevice(device, nullptr);
		device = VK_NULL_HANDLE;
	}
	if (owns_instance && instance && vk.DestroyInstance) {
		vk.DestroyInstance(instance, nullptr);
		instance = VK_NULL_HANDLE;
	}
	if (loader_lib) {
		FreeLibrary(static_cast<HMODULE>(loader_lib));
		loader_lib = nullptr;
	}
}

bool NtcVkDevice::begin_commands(std::string &out_error) {
	vk.ResetCommandBuffer(cmd, 0);
	VkCommandBufferBeginInfo bi{};
	bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (vk.BeginCommandBuffer(cmd, &bi) != VK_SUCCESS) {
		out_error = "vkBeginCommandBuffer failed.";
		return false;
	}
	return true;
}

bool NtcVkDevice::submit_and_wait(std::string &out_error) {
	if (vk.EndCommandBuffer(cmd) != VK_SUCCESS) {
		out_error = "vkEndCommandBuffer failed.";
		return false;
	}
	VkFenceCreateInfo fi{};
	fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence = VK_NULL_HANDLE;
	vk.CreateFence(device, &fi, nullptr, &fence);
	VkSubmitInfo si{};
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cmd;
	if (vk.QueueSubmit(queue, 1, &si, fence) != VK_SUCCESS) {
		vk.DestroyFence(device, fence, nullptr);
		out_error = "vkQueueSubmit failed.";
		return false;
	}
	vk.WaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
	vk.DestroyFence(device, fence, nullptr);
	return true;
}

uint32_t NtcVkDevice::find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags flags) const {
	for (uint32_t i = 0; i < memory_props.memoryTypeCount; ++i) {
		if ((type_bits & (1u << i)) && (memory_props.memoryTypes[i].propertyFlags & flags) == flags) {
			return i;
		}
	}
	return UINT32_MAX;
}

bool NtcVkDevice::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_flags,
		NtcVkBuffer &out, std::string &out_error) {
	out = {};
	out.size = size;
	VkBufferCreateInfo bi{};
	bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bi.size = size;
	bi.usage = usage;
	if (vk.CreateBuffer(device, &bi, nullptr, &out.buffer) != VK_SUCCESS) {
		out_error = "vkCreateBuffer failed.";
		return false;
	}
	VkMemoryRequirements req{};
	vk.GetBufferMemoryRequirements(device, out.buffer, &req);
	VkMemoryAllocateInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, mem_flags);
	if (ai.memoryTypeIndex == UINT32_MAX || vk.AllocateMemory(device, &ai, nullptr, &out.memory) != VK_SUCCESS) {
		out_error = "vkAllocateMemory (buffer) failed.";
		destroy_buffer(out);
		return false;
	}
	vk.BindBufferMemory(device, out.buffer, out.memory, 0);
	if (mem_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		vk.MapMemory(device, out.memory, 0, size, 0, &out.mapped);
	}
	return true;
}

void NtcVkDevice::destroy_buffer(NtcVkBuffer &buf) {
	if (!device) {
		buf = {};
		return;
	}
	if (buf.mapped && buf.memory) {
		vk.UnmapMemory(device, buf.memory);
	}
	if (buf.buffer) {
		vk.DestroyBuffer(device, buf.buffer, nullptr);
	}
	if (buf.memory) {
		vk.FreeMemory(device, buf.memory, nullptr);
	}
	buf = {};
}

bool NtcVkDevice::create_image(uint32_t width, uint32_t height, uint32_t layers, uint32_t mips, VkFormat format,
		VkImageUsageFlags usage, NtcVkImage &out, std::string &out_error, VkImageCreateFlags flags,
		const VkComponentMapping *swizzle) {
	out = {};
	out.format = format;
	out.width = width;
	out.height = height;
	out.layers = layers;
	out.mips = mips;
	VkImageCreateInfo ii{};
	ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ii.flags = flags;
	ii.imageType = VK_IMAGE_TYPE_2D;
	ii.format = format;
	ii.extent = { width, height, 1 };
	ii.mipLevels = mips;
	ii.arrayLayers = layers;
	ii.samples = VK_SAMPLE_COUNT_1_BIT;
	ii.tiling = VK_IMAGE_TILING_OPTIMAL;
	ii.usage = usage;
	ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (vk.CreateImage(device, &ii, nullptr, &out.image) != VK_SUCCESS) {
		out_error = "vkCreateImage failed.";
		return false;
	}
	VkMemoryRequirements req{};
	vk.GetImageMemoryRequirements(device, out.image, &req);
	VkMemoryAllocateInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (ai.memoryTypeIndex == UINT32_MAX || vk.AllocateMemory(device, &ai, nullptr, &out.memory) != VK_SUCCESS) {
		out_error = "vkAllocateMemory (image) failed.";
		destroy_image(out);
		return false;
	}
	vk.BindImageMemory(device, out.image, out.memory, 0);
	if (!create_image_view(out, 0, mips, 0, layers, out.view, out_error)) {
		destroy_image(out);
		return false;
	}
	if (swizzle && out.view) {
		vk.DestroyImageView(device, out.view, nullptr);
		out.view = VK_NULL_HANDLE;
		VkImageViewCreateInfo vi{};
		vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vi.image = out.image;
		vi.viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
		vi.format = format;
		vi.components = *swizzle;
		vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vi.subresourceRange.levelCount = mips;
		vi.subresourceRange.layerCount = layers;
		if (vk.CreateImageView(device, &vi, nullptr, &out.view) != VK_SUCCESS) {
			out_error = "vkCreateImageView (swizzle) failed.";
			destroy_image(out);
			return false;
		}
	}
	return true;
}

bool NtcVkDevice::create_image_view(const NtcVkImage &img, uint32_t base_mip, uint32_t mip_count, uint32_t base_layer,
		uint32_t layer_count, VkImageView &out, std::string &out_error) {
	VkImageViewCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vi.image = img.image;
	vi.viewType = (img.layers > 1 && layer_count > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
	vi.format = img.format;
	vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vi.subresourceRange.baseMipLevel = base_mip;
	vi.subresourceRange.levelCount = mip_count;
	vi.subresourceRange.baseArrayLayer = base_layer;
	vi.subresourceRange.layerCount = layer_count;
	if (vk.CreateImageView(device, &vi, nullptr, &out) != VK_SUCCESS) {
		out_error = "vkCreateImageView failed.";
		return false;
	}
	return true;
}

void NtcVkDevice::destroy_image(NtcVkImage &img) {
	if (!device) {
		img = {};
		return;
	}
	if (img.view) {
		vk.DestroyImageView(device, img.view, nullptr);
	}
	if (img.image) {
		vk.DestroyImage(device, img.image, nullptr);
	}
	if (img.memory) {
		vk.FreeMemory(device, img.memory, nullptr);
	}
	img = {};
}

void NtcVkDevice::barrier(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, VkAccessFlags src_access,
		VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, uint32_t layers,
		uint32_t mips) {
	VkImageMemoryBarrier b{};
	b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	b.srcAccessMask = src_access;
	b.dstAccessMask = dst_access;
	b.oldLayout = old_layout;
	b.newLayout = new_layout;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.image = image;
	b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	b.subresourceRange.levelCount = mips;
	b.subresourceRange.layerCount = layers;
	vk.CmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void NtcVkDevice::buffer_barrier(VkBuffer buffer, VkAccessFlags src_access, VkAccessFlags dst_access,
		VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkDeviceSize size) {
	VkBufferMemoryBarrier b{};
	b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	b.srcAccessMask = src_access;
	b.dstAccessMask = dst_access;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buffer;
	b.size = size;
	vk.CmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 1, &b, 0, nullptr);
}

VkShaderModule NtcVkDevice::create_shader(const void *spirv, size_t size, std::string &out_error) {
	VkShaderModuleCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	ci.codeSize = size;
	ci.pCode = static_cast<const uint32_t *>(spirv);
	VkShaderModule mod = VK_NULL_HANDLE;
	if (vk.CreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS) {
		out_error = "vkCreateShaderModule failed.";
		return VK_NULL_HANDLE;
	}
	return mod;
}
