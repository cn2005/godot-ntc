#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

struct NtcVkBuffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize size = 0;
	void *mapped = nullptr;
};

struct NtcVkImage {
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkFormat format = VK_FORMAT_UNDEFINED;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t layers = 1;
	uint32_t mips = 1;
};

class NtcVkDevice {
public:
	~NtcVkDevice();

	bool initialize(std::string &out_error);
	void shutdown();
	bool is_ready() const { return device != VK_NULL_HANDLE; }

	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t queue_family = 0;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	VkSampler latent_sampler = VK_NULL_HANDLE;
	VkPhysicalDeviceMemoryProperties memory_props{};
	VkPhysicalDeviceProperties gpu_props{};

	bool coop_vec_enabled = false;
	bool owns_instance = false;
	VkFormat latent_format = VK_FORMAT_UNDEFINED;
	VkComponentMapping latent_swizzle{};

	bool begin_commands(std::string &out_error);
	bool submit_and_wait(std::string &out_error);

	bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_flags, NtcVkBuffer &out,
			std::string &out_error);
	void destroy_buffer(NtcVkBuffer &buf);

	bool create_image(uint32_t width, uint32_t height, uint32_t layers, uint32_t mips, VkFormat format,
			VkImageUsageFlags usage, NtcVkImage &out, std::string &out_error, VkImageCreateFlags flags = 0,
			const VkComponentMapping *swizzle = nullptr);
	bool create_image_view(const NtcVkImage &img, uint32_t base_mip, uint32_t mip_count, uint32_t base_layer,
			uint32_t layer_count, VkImageView &out, std::string &out_error);
	void destroy_image(NtcVkImage &img);

	uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags flags) const;

	void barrier(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, VkAccessFlags src_access,
			VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
			uint32_t layers = 1, uint32_t mips = 1);
	void buffer_barrier(VkBuffer buffer, VkAccessFlags src_access, VkAccessFlags dst_access,
			VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkDeviceSize size);

	VkShaderModule create_shader(const void *spirv, size_t size, std::string &out_error);

	struct PFN {
		PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
		PFN_vkCreateInstance CreateInstance = nullptr;
		PFN_vkDestroyInstance DestroyInstance = nullptr;
		PFN_vkEnumerateInstanceVersion EnumerateInstanceVersion = nullptr;
		PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties = nullptr;
		PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
		PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
		PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures = nullptr;
		PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2 = nullptr;
		PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
		PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
		PFN_vkGetPhysicalDeviceFormatProperties GetPhysicalDeviceFormatProperties = nullptr;
		PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties = nullptr;
		PFN_vkCreateDevice CreateDevice = nullptr;
		PFN_vkDestroyDevice DestroyDevice = nullptr;
		PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
		PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
		PFN_vkCreateCommandPool CreateCommandPool = nullptr;
		PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
		PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
		PFN_vkFreeCommandBuffers FreeCommandBuffers = nullptr;
		PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
		PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
		PFN_vkResetCommandBuffer ResetCommandBuffer = nullptr;
		PFN_vkQueueSubmit QueueSubmit = nullptr;
		PFN_vkQueueWaitIdle QueueWaitIdle = nullptr;
		PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;
		PFN_vkCreateFence CreateFence = nullptr;
		PFN_vkDestroyFence DestroyFence = nullptr;
		PFN_vkWaitForFences WaitForFences = nullptr;
		PFN_vkResetFences ResetFences = nullptr;
		PFN_vkCreateBuffer CreateBuffer = nullptr;
		PFN_vkDestroyBuffer DestroyBuffer = nullptr;
		PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = nullptr;
		PFN_vkAllocateMemory AllocateMemory = nullptr;
		PFN_vkFreeMemory FreeMemory = nullptr;
		PFN_vkBindBufferMemory BindBufferMemory = nullptr;
		PFN_vkMapMemory MapMemory = nullptr;
		PFN_vkUnmapMemory UnmapMemory = nullptr;
		PFN_vkCreateImage CreateImage = nullptr;
		PFN_vkDestroyImage DestroyImage = nullptr;
		PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements = nullptr;
		PFN_vkBindImageMemory BindImageMemory = nullptr;
		PFN_vkCreateImageView CreateImageView = nullptr;
		PFN_vkDestroyImageView DestroyImageView = nullptr;
		PFN_vkCreateSampler CreateSampler = nullptr;
		PFN_vkDestroySampler DestroySampler = nullptr;
		PFN_vkCreateShaderModule CreateShaderModule = nullptr;
		PFN_vkDestroyShaderModule DestroyShaderModule = nullptr;
		PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = nullptr;
		PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout = nullptr;
		PFN_vkCreatePipelineLayout CreatePipelineLayout = nullptr;
		PFN_vkDestroyPipelineLayout DestroyPipelineLayout = nullptr;
		PFN_vkCreateComputePipelines CreateComputePipelines = nullptr;
		PFN_vkDestroyPipeline DestroyPipeline = nullptr;
		PFN_vkCreateDescriptorPool CreateDescriptorPool = nullptr;
		PFN_vkDestroyDescriptorPool DestroyDescriptorPool = nullptr;
		PFN_vkAllocateDescriptorSets AllocateDescriptorSets = nullptr;
		PFN_vkUpdateDescriptorSets UpdateDescriptorSets = nullptr;
		PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;
		PFN_vkCmdBindPipeline CmdBindPipeline = nullptr;
		PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = nullptr;
		PFN_vkCmdDispatch CmdDispatch = nullptr;
		PFN_vkCmdCopyBuffer CmdCopyBuffer = nullptr;
		PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage = nullptr;
		PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer = nullptr;
	} vk;

private:
	void *loader_lib = nullptr;
	bool load_loader(std::string &out_error);
	void load_instance_table();
	void load_device_table();
	bool create_instance(std::string &out_error);
	bool pick_device(std::string &out_error);
	bool create_logical_device(std::string &out_error);
};
