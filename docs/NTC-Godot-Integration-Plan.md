# RTX Neural Texture Compression × Godot 引擎集成落地方案

> 目标引擎：Godot 4.6.x / 4.7（Vulkan Forward+ / Mobile）
> 目标 SDK：RTX NTC SDK v0.10.0 BETA（本仓库）
> 文档状态：技术方案 v1（已完成可行性核实，含实测结论）
> 产品切分与引擎接触面已收紧，见 [NTC-Godot-Architecture.md](./NTC-Godot-Architecture.md)（架构 v2）。本文第 2 节的约束仍成立；「一个材质三档 / P2 注入主 Forward+ 模板」以 v2 为准。

---

## 0. 结论摘要（先读这一节）

本方案交付三件东西：

1. **`ntc_godot` GDExtension**（C++）：运行时 `.ntc` 装载 + 解压 + BCn 转码，以及编辑器侧的压缩引擎封装。
2. **`addons/ntc` 编辑器插件**（GDScript + 上述 GDExtension）：可交互的压缩页面、批量转换、质量评估（PSNR / 差分预览）、导入管线。
3. **`godot-ntc-patch` 引擎补丁包**：一组针对 Godot 源码的补丁 + 一个引擎 module，用于开启 Vulkan 设备特性、注入 NTC 推理到场景着色器、扩展材质系统。

两种解压路径的可行性判定是不对称的，这是整个方案的核心结论：

| 路径 | 收益 | 是否需要改引擎 | 结论 |
|---|---|---|---|
| **加载时推理**（Inference on Load）→ 转码为 BCn | 只优化磁盘/分发体积，显存与 BCn 持平 | **不需要**（可在原版 Godot 上跑） | 风险低，优先落地 |
| **采样时推理**（Inference on Sample）→ 着色器内解码 | 同时优化显存（约 5×）与存储 | **必须改引擎**（无绕过方案） | 风险高，第二阶段 |

必须改引擎的原因在第 2 节给出实测证据，简要说：Godot 的着色语言没有 SSBO、没有 8-bit 点积内建、无法声明自定义描述符集，而 NTC 的 MLP 每像素需要 8192 次 MAC，必须靠 `dot4add_i8packed` / CooperativeVector 才能实时；这三件事都落在 Godot 着色器编译管线内部，GDExtension 触碰不到。

---

## 1. 背景与目标

### 1.1 NTC 是什么

NTC 把一个材质的多张相关贴图（albedo / normal / roughness / metalness / AO / opacity…，总计最多 16 个通道）压进一份神经表示：**latent 特征网格**（存成 `Texture2DArray`，BGRA4 格式）+ **一个 4 层 MLP 的权重**（几十 KB）。解码时 MLP 一次性吐出全部 16 个通道。

MLP 规模（`libraries/RTXNTC-Library/include/libntc/shaders/InferenceConstants.h`）：

```
输入 48 → 隐藏 64 → 隐藏 48 → 隐藏 32 → 输出 16
每 texel 约 8192 次乘加
```

典型码率约 **5 bpp**，对比原始 64 bpp、BCn 打包约 24 bpp。2K 材质包：NTC ≈ 2.5 MB，BCn ≈ 12 MB。

### 1.2 项目目标

- **G1** 在 Godot 中提供「加载时推理」：`.ntc` 随包分发，加载时 GPU 解码并转码成 BC1/3/4/5/6/7，塞回普通 `StandardMaterial3D`。**收益：包体积**。
- **G2** 在 Godot 中提供「采样时推理」：`.ntc` 常驻显存，像素着色器内解码 + STF 随机纹理过滤。**收益：包体积 + 显存**。
- **G3** 压缩功能做成编辑器插件的可交互页面：导入贴图 → 配语义 → 调 BPP/目标 PSNR → 实时看 PSNR 与差分 → 导出 `.ntc`，并支持批量。
- **G4** 平台不支持时能优雅降级，游戏照常能跑。

### 1.3 非目标（本期不做）

- Inference on Feedback（Sampler Feedback + 稀疏瓦片流送）：依赖 DX12 Sampler Feedback 与 `RTXTS-TTM`，Godot 无稀疏纹理 API，列入 v2 路线图。
- macOS / Metal、Android、Web：NTC 无 Metal/GLES 后端，走降级路径。
- 运行时压缩：压缩依赖 CUDA，只在编辑器 / 构建机上跑。

---

## 2. 可行性核实（实测结论，方案的地基）

这一节的每条结论都对着源码验证过，后面的架构选择全部由它们推导而来。

### 2.1 Godot 的 Vulkan 设备没有开 NTC 需要的特性

NTC 文档（`docs/integration/Context.md`）要求宿主在 `vkCreateDevice` 时开启：

```
VkPhysicalDeviceFeatures::shaderInt16
VkPhysicalDeviceVulkan11Features::storageBuffer16BitAccess
VkPhysicalDeviceVulkan12Features::shaderFloat16
VkPhysicalDeviceVulkan12Features::storageBuffer8BitAccess
VkPhysicalDeviceVulkan13Features::shaderDemoteToHelperInvocation
VkPhysicalDeviceVulkan13Features::shaderIntegerDotProduct
（可选）VK_NV_cooperative_vector + VkPhysicalDeviceCooperativeVectorFeaturesNV::cooperativeVector
```

查 Godot 4.6 的 `drivers/vulkan/rendering_device_driver_vulkan.cpp::_initialize_device()`，它构造的 `pNext` 链只有：`VkPhysicalDeviceShaderFloat16Int8FeaturesKHR`（开了 `shaderFloat16` / `shaderInt8`）、`BufferDeviceAddress`、`VulkanMemoryModel`、`FragmentShadingRate`、`FragmentDensityMap`、`PipelineCreationCacheControl`、`Fault`、`VkPhysicalDeviceVulkan11Features`（开了 16-bit storage 与 multiview）。

**缺失**：`storageBuffer8BitAccess`、`shaderIntegerDotProduct`、`shaderDemoteToHelperInvocation`。没有 `VkPhysicalDeviceVulkan12Features` / `Vulkan13Features` 结构体参与设备创建。

→ NTC 的预编译 SPIR-V **无法在 Godot 自己的 VkDevice 上创建管线**。

Godot 4.6 新增的项目设置 `rendering/rendering_device/vulkan/additional_device_extensions` 只能追加**扩展名**，不能追加**特性位**，解决不了这个问题（扩展开了但 feature 没开，SPIR-V 校验照样失败）。

### 2.2 NTC 解压 pass 用的是 bindless 描述符表

`libraries/RTXNTC-Library/include/libntc/shaders/Bindings.h` + `libraries/ntc-utils/src/GraphicsDecompressionPass.cpp`：

```
set 0: b0 常量缓冲 / t1 latent Texture2DArray / t2 权重 ByteAddressBuffer / s3 采样器
set 1: u0[] —— 无界的 RWTexture2D<float4> 数组（nvrhi createBindlessLayout）
```

Godot 的 `RenderingDevice` 只支持定长数组的 uniform，其 SPIR-V 反射不接受 runtime descriptor array。

→ 即使设备特性齐了，也**不能**用 `RenderingDevice.shader_create_from_spirv()` 跑 NTC 的解压 shader。

### 2.3 Godot 着色语言表达不了 NTC 的采样时推理

- 没有 SSBO：权重缓冲（`ByteAddressBuffer`）无法声明。可以退化成把权重塞进 `usampler2D`，但……
- 没有 `dot4add_i8packed` / `GL_EXT_integer_dot_product`：只能用标量整数模拟，8192 次 MAC 全展开，1080p 下约 16 G 次整数运算/帧，**不可用**。
- 没有 `float16_t`、没有 CooperativeVector。
- 没法在材质里增加自定义描述符集。

→ 采样时推理只能在引擎内部（GLSL 模板 + 着色器编译器 + 材质存储）实现。

### 2.4 latent 纹理格式在 Godot 枚举里缺一个

NTC latent 纹理固定为 `VK_FORMAT_A4R4G4B4_UNORM_PACK16`（来自 `VK_EXT_4444_formats`），Godot 的 `RenderingDevice.DataFormat` 里只有核心格式，没有它。

**解决**：用核心格式 `VK_FORMAT_R4G4B4A4_UNORM_PACK16`（Godot 里是 `DATA_FORMAT_R4G4B4A4_UNORM_PACK16`）承载同样的 16-bit 字，再用 `VkImageView` 的 component swizzle 纠正分量顺序：

```
A4R4G4B4 位布局：A[15:12] R[11:8] G[7:4] B[3:0]
R4G4B4A4 位布局：R[15:12] G[11:8] B[7:4] A[3:0]
→ swizzle: r←G, g←B, b←A, a←R
```

如果我们自己建 image（方案里确实如此），直接用 `A4R4G4B4` + `VK_EXT_4444_formats` 即可，swizzle 方案是给「必须走 Godot RD」的场景留的后路。

### 2.5 但是——跨 RenderingDevice 共享图像是可行的

`RenderingDevice.texture_create_from_extension(type, format, samples, usage, image_handle, w, h, d, layers, mipmaps)` 接受一个原生 `VkImage` 句柄并包成 Godot 的纹理 RID；再赋给 `Texture2DRD.texture_rd_rid` 就能直接喂给材质。社区已广泛用它在 local RenderingDevice 与主 RD 之间共享纹理。

**关键推论**：只要目标 BCn 图像是**在 Godot 的 VkDevice 上**创建的（我们用原生 Vulkan 自己 `vkCreateImage`），就能零拷贝交给 Godot；至于计算 pass 在哪个设备上跑，是另一个问题（见 4.2 的三个方案）。

### 2.6 平台能力矩阵

| 平台 / 后端 | 压缩 | 加载时推理 | 采样时推理 |
|---|---|---|---|
| Windows x64 Vulkan + NVIDIA | ✅ CUDA | ✅ | ✅（CoopVec，Ada+ 推荐） |
| Windows x64 Vulkan + AMD/Intel | ❌ | ✅（DP4a） | ⚠️ 仅 DP4a，性能不足 |
| Linux x64 Vulkan | ✅ CUDA / ✅ | ✅ | ✅ NVIDIA |
| Windows D3D12 后端 | — | ✅（NTC 支持 DX12） | ⚠️ LinAlg 仍是 preview，禁止发版 |
| macOS / Metal、Android、Web | ❌ | ❌ | ❌ |

→ 降级策略是硬需求（第 7.4 节）。

---

## 3. 总体架构

```
┌──────────────────────── 编辑器 / 构建期 ────────────────────────┐
│  addons/ntc (GDScript EditorPlugin)                             │
│   ├─ NTC 压缩面板（Dock）      ├─ 批量转换向导                   │
│   ├─ 材质检查器扩展            └─ 项目设置页                     │
│         ↓ 调用                                                  │
│  ntc_godot GDExtension —— 编辑器侧                               │
│   ├─ NtcCompressor      (libntc CUDA: ITextureSet 压缩/自适应)   │
│   ├─ NtcQualityEval     (PSNR / 逐通道误差 / 差分图)             │
│   └─ NtcImporter        (.ntcbundle → .ntc；.ntc → 资源)         │
└─────────────────────────────────────────────────────────────────┘
                              ↓  .ntc 资产
┌──────────────────────── 运行时 ─────────────────────────────────┐
│  ntc_godot GDExtension —— 运行时侧                               │
│   ├─ NtcRuntime       单例：能力探测、设备/队列、常驻资源池       │
│   ├─ NtcVkBackend     原生 Vulkan：管线、描述符、命令提交         │
│   ├─ NTCTextureSet    Resource：.ntc 句柄 + 语义映射             │
│   ├─ NTCLoader        ResourceFormatLoader（支持线程化加载）      │
│   └─ NTCMaterial3D    Resource：面向采样时推理的材质              │
└─────────────────────────────────────────────────────────────────┘
                              ↓  仅采样时推理需要
┌──────────────────────── 引擎补丁 ────────────────────────────────┐
│  godot-ntc-patch                                                 │
│   ├─ P1 Vulkan 设备特性 + 扩展开启                                │
│   ├─ P2 scene_forward_clustered 着色器模板注入 NTC 解码 + STF     │
│   ├─ P3 材质存储：NTC uniform set（latent / weights / 常量）      │
│   └─ P4 着色语言内建函数 ntc_sample_*                             │
└──────────────────────────────────────────────────────────────────┘
```

### 3.1 资源模型

```gdscript
# NTCTextureSet —— 对应一个 .ntc 文件
class_name NTCTextureSet extends Resource
    @export var ntc_path: String              # res:// 路径（导出时原样打包）
    @export var decode_mode: DecodeMode       # AUTO / ON_LOAD / ON_SAMPLE / FALLBACK_BCN
    @export var semantic_map: Dictionary      # "albedo" -> {tex_index, first_channel, num_channels}
    @export var fallback_textures: Array[Texture2D]  # 不支持平台的预烘焙 BCn
    var width: int, height: int, mips: int, bits_per_pixel: float, psnr: float  # 只读元数据
```

`NTCTextureSet` 是纯数据；**解码产物**由 `NtcRuntime` 管理并缓存，多个材质引用同一 `.ntc` 时只解一次。

### 3.2 三种运行模式

- **ON_LOAD**：加载时解码 → BCn → 返回一组 `Texture2D`，直接挂到 `StandardMaterial3D`。对现有项目零侵入。
- **ON_SAMPLE**：常驻 latent + 权重，配 `NTCMaterial3D` 使用，需要补丁引擎。
- **FALLBACK_BCN**：走 `fallback_textures`，即导出期预转码的 `.ctex`。

`AUTO` 由 `NtcRuntime.get_capability_tier()` 在启动时决定。

---

## 4. 模式一：加载时推理（Inference on Load）

### 4.1 数据流

```
.ntc 文件
  │ ① OpenFile + CreateTextureSetMetadataFromStream        （纯 CPU，无需 GPU）
  ├─→ ITextureSetMetadata：纹理枚举、通道映射、BC 目标格式、色彩空间
  │
  │ ② GetLatentTextureDesc / GetLatentTextureFootprint(mip, layer)
  │    读文件 →（可选 GDeflate 解压）→ 上传到 Texture2DArray(A4R4G4B4)
  │
  │ ③ GetBestSupportedWeightType() → GetInferenceWeights()
  │    上传权重；若 convertedSize>0 则 ConvertInferenceWeights() 在 GPU 上转 CoopVec 布局
  │
  │ ④ 逐 color mip：MakeDecompressionComputePass() → ComputePassDesc
  │    { SPIR-V 字节码, 常量缓冲数据(~672B), dispatchWidth/Height }
  │    dispatch → RWTexture2D<float4> 中间纹理（每个逻辑纹理一张）
  │
  │ ⑤ 逐纹理：MakeBlockCompressionComputePass(dstFormat=BC7/BC5/BC4…)
  │    （BC7 可用 GetBC7ModeBufferFootprint 的 mode buffer 加速 20~30×）
  │    dispatch → RWTexture2D<uint4> （块分辨率）
  │
  │ ⑥ uint4 图像 → BCn 图像
  └─→ Godot Texture2D
```

第 ⑥ 步有个干净的做法：创建 BCn `VkImage` 时带上 `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT`，再建一个 `R32G32B32A32_UINT`（BC1/BC4 用 `R32G32_UINT`）的 storage view，BC 编码 pass 直接写进去，**零拷贝零转换**。退而求其次可以用 `vkCmdCopyImage`——Vulkan 允许在 size-compatible 的压缩/非压缩图像间拷贝（BC7 块 16 字节 ↔ RGBA32UI 16 字节）。

### 4.2 三个部署形态（按引擎侵入度递增）

#### 形态 A1：原版 Godot + 独立 VkDevice + 回读（保底方案）

- GDExtension 自建 `VkDevice`（复用 Godot 的 `VkInstance` 与 `VkPhysicalDevice`，通过 `get_driver_resource(DRIVER_RESOURCE_TOPMOST_OBJECT / PHYSICAL_DEVICE)` 取得），开齐 NTC 需要的全部特性。
- 全部 NTC pass 在该设备上跑，BCn 块数据 `vkCmdCopyImageToBuffer` 回读到 host buffer。
- `Image.create_from_data(w, h, mipmaps, Image.FORMAT_BPTC_RGBA, bytes)` → `ImageTexture.create_from_image()`。

代价：一次 GPU→CPU→GPU 往返。2K BC7 单张约 5.6 MB，一套 4 张材质约 15 MB，PCIe 4.0 下约 2–3 ms 传输 + Godot 侧上传。放在 `WorkerThreadPool` 里做流式加载完全可接受。

**优点**：对 Godot 版本零要求，任何 4.x + Vulkan 都能跑。作为兜底与 CI 基线。

#### 形态 A2：Godot 4.6 + 外部内存（零拷贝，无需改引擎）

利用 Godot 4.6 的 `additional_device_extensions` 项目设置（插件可自动写入）：

```ini
[rendering]
rendering_device/vulkan/additional_device_extensions=PackedStringArray(
    "VK_KHR_external_memory_win32", "VK_KHR_external_semaphore_win32")
# Linux 换成 VK_KHR_external_memory_fd / VK_KHR_external_semaphore_fd
```

- NTC 设备上创建 BCn image 时用 `VkExportMemoryAllocateInfo` 导出句柄；
- Godot 设备上 `vkAllocateMemory` + `VkImportMemoryWin32HandleInfoKHR` 导入，`vkCreateImage` 绑定；
- `RenderingDevice.texture_create_from_extension(...)` → `Texture2DRD`。
- 跨设备同步用导出的 `VkSemaphore`（或保守地用 fence + 主线程栅栏）。

**优点**：零回读，原版 Godot 4.6 可用。**缺点**：Windows/Linux 分平台代码，外部内存对齐/`dedicatedAllocation` 细节多，调试成本高。

#### 形态 A3：补丁引擎（推荐的最终形态）

引擎补丁 P1 在 `_initialize_device()` 里补上特性链（见 5.1）。之后：

- NTC 的 pass 直接在 **Godot 的 VkDevice** 上跑（自建 `VkCommandPool` + 用 `DRIVER_RESOURCE_COMMAND_QUEUE` / `DRIVER_RESOURCE_QUEUE_FAMILY` 取队列，或申请独立的 transfer/compute 队列避免与主渲染争用）；
- 目标 BCn image 本来就在 Godot 设备上 → `texture_create_from_extension` 直接包。
- 完全零拷贝、无跨设备同步。

> 决策建议：**A1 先跑通功能与正确性 → A3 作为发版形态 → A2 只在"必须用官方二进制 Godot"的项目里启用**。三者共用同一套 `NtcVkBackend` 抽象，差异隔离在「设备从哪来」「产物怎么交付」两个接口后面。

### 4.3 关键实现细节

#### 描述符布局（我们自己建，不走 Godot RD）

```cpp
// 解压 pass
VkDescriptorSetLayout set0:  // NTC_BINDING_DECOMPRESSION_INPUT_SPACE
  binding 0: UNIFORM_BUFFER          // 常量缓冲，ComputePassDesc::constantBufferData
  binding 1: SAMPLED_IMAGE           // latent Texture2DArray (A4R4G4B4)
  binding 2: STORAGE_BUFFER (只读)    // 权重 ByteAddressBuffer
  binding 3: SAMPLER                 // 双线性 + WRAP（必须，见 LatentTextureDesc 注释）

VkDescriptorSetLayout set1:  // NTC_BINDING_DECOMPRESSION_OUTPUT_SPACE
  binding 0: STORAGE_IMAGE, descriptorCount = 12 (DECOMPRESS_CS_MAX_OUTPUTS)
             flags = PARTIALLY_BOUND | VARIABLE_DESCRIPTOR_COUNT
             需要 descriptorIndexing::runtimeDescriptorArray

// BC 编码 pass —— 单个 set
  binding 0: UNIFORM_BUFFER   binding 1: SAMPLED_IMAGE
  binding 2: STORAGE_BUFFER (BC7 mode buffer，可选)   binding 3: STORAGE_IMAGE
```

NVRHI 参考实现里 `VulkanBindingOffsets` 全设为 0，说明 DXC 生成的 SPIR-V 就是按上表的 set/binding 直接编号，无需偏移。

#### 常量缓冲

`ComputePassDesc::constantBufferData` 最大 `MaxComputePassConstantSize = 688` 字节，解压 pass 实际 `sizeof(NtcDecompressConstants)` ≈ 672。每个 mip / 每个 tile 都要重新 `MakeDecompressionComputePass()` 拿一份新的常量数据（这个调用很便宜，不分配资源），所以用一个 ring buffer 存常量，dynamic offset 绑定。

Dispatch：解压 pass 线程组 16×8；BC 编码 16×8（BC7 是 32×16）。`dispatchWidth/Height` 由 libntc 直接给出，不要自己算。

#### BCn → Godot 格式映射

| NTC `BlockCompressedFormat` | Vulkan | Godot `Image.Format` | 典型用途 |
|---|---|---|---|
| BC1 | `BC1_RGB_UNORM_BLOCK` / `_SRGB` | `FORMAT_DXT1` | 不透明 albedo |
| BC3 | `BC3_UNORM_BLOCK` / `_SRGB` | `FORMAT_DXT5` | albedo + alpha |
| BC4 | `BC4_UNORM_BLOCK` | `FORMAT_RGTC_R` | roughness / AO / mask |
| BC5 | `BC5_UNORM_BLOCK` | `FORMAT_RGTC_RG` | 法线 |
| BC6（无符号） | `BC6H_UFLOAT_BLOCK` | `FORMAT_BPTC_RGBFU` | HDR |
| BC7 | `BC7_UNORM_BLOCK` / `_SRGB` | `FORMAT_BPTC_RGBA` | 通用高质量 |

sRGB 由 `ITextureMetadata::GetRgbColorSpace()` 决定；Godot 侧 `StandardMaterial3D` 的 albedo 槽期望 sRGB 纹理，法线/ORM 期望线性——映射表在 `NtcSemanticMapper` 里集中维护。

#### Mip 链

`GetFusedMipLevels(mip)` 会告诉你哪些 color mip 共享同一份 latent——同一组 mip 一次 dispatch 就能全出，别逐 mip 重复上传 latent。完整 mip 链的处理顺序：按 latent image 分组 → 每组一次解压 → 组内逐 mip 做 BC 编码。

#### GDeflate

latent 与 BC7 mode buffer 可能是 GDeflate 压缩的（`BufferFootprint::compressionType`）。三条解压路径按可用性排序：

1. `VK_NV_memory_decompression` → `IContext::DecompressGDeflateOnVulkanGPU()`（最快，NVIDIA）
2. `IContext::DecompressBuffer()` CPU 解压（通用兜底，放 worker 线程）
3. 压缩期直接关掉 latent 的 GDeflate（`ConfigureLosslessCompression` / `--gdeflate` 只压 mode buffer）——文档明说 latent 的无损压缩收益本来就不大，对首发版本这是最省事的选择。

#### 线程模型

```
主线程          ResourceLoader.load_threaded_request("res://mat.ntc")
  │
Godot WorkerThread (Godot 的资源加载线程)
  │  ① 读文件 / 解析元数据 / CPU GDeflate      —— 纯 CPU，安全
  │  ② 提交 NTC 命令缓冲到专用 compute 队列     —— 需要队列互斥锁
  │  ③ 等 fence
  │  ④ （A1）回读 → Image；（A3）直接包 Texture2DRD
  │
主线程          load_threaded_get() → 拿到 Texture2D
```

注意：`ITextureSet` 非线程安全，`IContext` 线程安全。`NtcRuntime` 对每个加载线程持有独立的 command pool 与 staging buffer 池；队列提交加全局锁（Godot 每个 queue family 只创建 1 个 queue）。A3 形态下如果和 Godot 主渲染共用队列，必须用 `RenderingServer.call_on_render_thread()` 提交以避免竞态。

### 4.4 显存与耗时预算（2K、5 通道材质包）

| 项 | A1 | A3 |
|---|---|---|
| latent 常驻 | 临时 ~2.5 MB | 临时 ~2.5 MB |
| 中间 float4 纹理 | 临时 ~64 MB（可复用池） | 同左 |
| 最终 BCn | ~12 MB（Godot 侧） | ~12 MB |
| 额外回读 | ~12 MB host + 传输 | 0 |
| 预估耗时 | 解压 ~1 ms + BC7(带 mode buffer) ~1 ms + 回读 ~3 ms | ~2 ms |

中间 float4 纹理是峰值显存大头，务必做**尺寸分档的复用池**，并支持按 tile 分块解压（`MakeDecompressionComputePassParameters::pSrcRect` + `pDstOffset`）以压低峰值。

---

## 5. 模式二：采样时推理（Inference on Sample）

这是显存收益的来源，也是全部工程风险的来源。必须基于**自定义 Godot 构建**。

### 5.1 P1：Vulkan 设备特性与扩展

修改 `drivers/vulkan/rendering_device_driver_vulkan.cpp`：

```cpp
// _initialize_device_extensions()
_register_requested_device_extension(VK_KHR_8BIT_STORAGE_EXTENSION_NAME, false);
_register_requested_device_extension(VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME, false);
_register_requested_device_extension(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME, false);
_register_requested_device_extension(VK_EXT_4444_FORMATS_EXTENSION_NAME, false);
_register_requested_device_extension("VK_NV_cooperative_vector", false);

// _initialize_device()  —— 追加到 pNext 链
VkPhysicalDeviceVulkan12Features vk12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
vk12.shaderFloat16           = ntc_caps.shader_float16;
vk12.storageBuffer8BitAccess = ntc_caps.storage_buffer_8bit;
vk12.runtimeDescriptorArray  = ntc_caps.runtime_descriptor_array;
vk12.descriptorBindingPartiallyBound = ...;
vk12.pNext = create_info_next;  create_info_next = &vk12;

VkPhysicalDeviceVulkan13Features vk13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
vk13.shaderDemoteToHelperInvocation = ...;
vk13.shaderIntegerDotProduct        = ...;
vk13.pNext = create_info_next;  create_info_next = &vk13;

VkPhysicalDeviceCooperativeVectorFeaturesNV coopvec = {...};
if (coopvec_supported) { coopvec.cooperativeVector = VK_TRUE; /* 挂链 */ }
```

所有位都先用 `vkGetPhysicalDeviceFeatures2` 查询，**支持才开**，并把结果暴露成 `RenderingDevice.has_feature(FEATURE_NTC_*)`，让 GDExtension 能探测。

> 这个补丁小而独立，是**最值得向上游提 PR** 的部分（提议形式：`additional_device_features` 项目设置 + 能力查询 API），一旦合入，A3 形态就不再需要自定义构建。

### 5.2 P2：着色器管线——把 NTC 解码注入 GLSL

Godot 的 `.gdshader` → GLSL → glslang → SPIR-V。NTC 的 `Inference.hlsli`（359 行）与 `InferenceCoopVec.hlsli`（408 行）是 HLSL，需要移植：

| HLSL 构造 | GLSL 对应 | 风险 |
|---|---|---|
| `Texture2DArray` + `SampleLevel` | `sampler2DArray` + `textureLod` | 低 |
| `ByteAddressBuffer.Load<T>` | `readonly buffer { uint data[]; }` + 手动拆包 | 低 |
| `dot4add_i8packed(a,b,acc)` | `GL_EXT_integer_dot_product` 的 `dotPacked4x8AccSatEXT` | 中：需确认 glslang 版本支持 |
| `half` / `min16float` | `GL_EXT_shader_explicit_arithmetic_types_float16` 的 `float16_t` | 低 |
| CoopVec `__builtin` / Slang intrinsics | `GL_NV_cooperative_vector` GLSL 扩展 | **高**：glslang 支持成熟度待验证 |
| `ColorSpaces.hlsli`（sRGB / HLG） | 直译 | 低 |

**CoopVec 的兜底方案**：如果 glslang 的 `GL_NV_cooperative_vector` 不可用，改用「离线 Slang 编译 NTC 推理为 SPIR-V 函数库 + `spirv-link` 与 Godot 编译产物链接」。这需要在 Godot 的 `RenderingShaderContainer` 流程里插一个链接步骤，属于 P2 的备选分支，**必须在 M5 的技术预研里先做掉**，它是全项目最大的单点风险。

移植后的产物：`servers/rendering/renderer_rd/shaders/scene/ntc_inference.glsl`（`#include` 进 `scene_forward_clustered.glsl`），以及一个 `NTC_INFERENCE` 着色器变体宏，避免不用 NTC 的材质付出编译/寄存器代价。

### 5.3 P3：材质系统扩展

新增 uniform set（或复用 set 3 材质集的空余 binding）：

```glsl
layout(set = 4, binding = 0) uniform sampler2DArray ntc_latents;
layout(set = 4, binding = 1) readonly buffer NtcWeights { uint data[]; } ntc_weights;
layout(set = 4, binding = 2) uniform NtcConstants { /* NtcTextureSetConstants，~320B */ } ntc_const;
```

`NtcTextureSetConstants` 的 shader 侧定义已经贴心地把结构体打平成 `int4[]`（见 `InferenceConstants.h` 的 `#else` 分支，注释写明「for compatibility with engines that don't support structs in constant buffers」），直接复用即可。

`MaterialStorage` 侧新增 `NTCMaterialData`，持有上述三个资源的 RID，在 `bind_uniforms` 时绑定。多个材质共享同一 `.ntc` 时共享同一个 set。

### 5.4 P4：着色语言内建函数

在 `shader_language.cpp` 注册、在 `shader_compiler.cpp` 生成 GLSL：

```glsl
// 用户在 .gdshader 里这样写：
shader_type spatial;
render_mode neural_textures;   // 触发 NTC_INFERENCE 变体 + set 4 绑定

void fragment() {
    NtcSample s = ntc_sample(UV);          // 一次解码，拿全部 16 通道
    ALBEDO    = ntc_channels3(s, 0);
    NORMAL_MAP= ntc_channels3(s, 3);
    ROUGHNESS = ntc_channel (s, 6);
    METALLIC  = ntc_channel (s, 7);
    AO        = ntc_channel (s, 8);
}
```

同时提供一个开箱即用的 `NTCMaterial3D`（C++ 侧，仿 `BaseMaterial3D`），自动按 `semantic_map` 生成上述着色器，让美术不用写 shader。

### 5.5 STF（随机纹理过滤）

NTC 采样时推理**没有硬件过滤**——`NtcSampleTextureSet` 接受的是整数 texel 坐标 + mip 层级，一次只出一个 texel。必须用 STF 抖动采样 + 时域降噪。

- 移植 `libraries/RTXTF-Library/STFSamplerState.hlsli` 等到 GLSL（参考实现见 `samples/renderer/NtcForwardShadingPass.hlsl`，178 行）。
- 需要每帧的随机种子 uniform（帧号 + 屏幕坐标哈希）。
- **硬性前提**：项目必须开启 TAA 或 FSR2/DLSS。Godot 内置 TAA 质量一般，需要在文档里明确写死这个约束，并在 `NTCMaterial3D` 检测到 TAA 关闭时在编辑器里报警告。
- 对 alpha cutout：按 `docs/SettingsAndQuality.md` 的建议，**把 opacity 单独存成 BC4**，不要走 NTC 解码——STF 会把 cutout 边缘抖成噪点，而且 Z-prepass / 阴影 pass 为了一个 alpha 去跑整个 MLP 得不偿失。

### 5.6 性能预算与降级

| GPU | 路径 | 1080p 全屏 NTC 材质预估开销 |
|---|---|---|
| RTX 40/50 | CoopVec FP8 | 可接受（NVIDIA 报告与 BCn 采样同量级 × 数倍） |
| RTX 20/30 | CoopVec（驱动 570+） | 明显开销，建议限制 NTC 材质屏占比 |
| 非 NVIDIA | DP4a | 仅供功能验证，不建议发版 |

降级策略：`NtcRuntime` 在启动时跑一次微基准（离屏 dispatch 已知规模的推理），若低于阈值自动把 `AUTO` 模式降到 `ON_LOAD`。

---

## 6. 压缩功能：可交互的编辑器插件

### 6.1 插件结构

```
addons/ntc/
├── plugin.cfg
├── ntc_plugin.gd                 # EditorPlugin 入口
├── ui/
│   ├── compressor_dock.tscn/.gd  # 主压缩面板
│   ├── bundle_editor.tscn/.gd    # 语义槽位编辑
│   ├── quality_panel.tscn/.gd    # PSNR 曲线 + 差分预览
│   ├── batch_wizard.tscn/.gd     # 批量转换
│   └── settings_page.tscn/.gd    # 项目设置
├── importer/
│   ├── ntc_importer.gd           # EditorImportPlugin: .ntc → NTCTextureSet
│   └── bundle_importer.gd        # EditorImportPlugin: .ntcbundle → 触发压缩
├── inspector/
│   └── material_inspector.gd     # EditorInspectorPlugin: 材质上的「转为 NTC」按钮
└── bin/                          # ntc_godot GDExtension 二进制 + libntc.dll + cudart
```

### 6.2 主压缩面板（Dock）交互设计

```
┌─ NTC 压缩器 ────────────────────────────────────────────────┐
│ 材质包：PavingStones070                    [新建] [载入] [保存]│
├──────────────────────────────────────────────────────────────┤
│ 通道分配 (0/16 已用)                                          │
│  ┌────────────┬────────┬──────┬──────┬───────┬────────┐      │
│  │ 贴图        │ 语义   │ 通道 │ sRGB │ BC 格式│ 权重   │      │
│  ├────────────┼────────┼──────┼──────┼───────┼────────┤      │
│  │ diffuse.png│ Albedo │ 0-2  │  ☑   │  BC7  │ [1.0]  │      │
│  │ normal.png │ Normal │ 3-5  │  ☐   │  BC7  │ [2.0]  │      │
│  │ rough.png  │Roughness│  6   │  ☐   │  BC4  │ [1.0]  │      │
│  │ ao.png     │ AO     │  7   │  ☐   │  BC4  │ [0.5]  │      │
│  │            [＋ 拖入贴图或点击添加]                  │      │
│  └────────────┴────────┴──────┴──────┴───────┴────────┘      │
├──────────────────────────────────────────────────────────────┤
│ 压缩设置                                                      │
│  模式  ( • ) 固定码率   (   ) 目标质量                        │
│  BPP   [━━━━●━━━━━━━] 5.0    → latent 4×16，预估 2.48 MB      │
│  质量  目标 PSNR [40.0] dB   （约 5× 耗时）                   │
│  Mip   ☑ 生成完整 mip 链    ☑ 生成 BC7 mode buffer（加速转码）│
│  高级  ☑ Alpha mask 特殊处理  ☐ 丢弃被 mask 的像素            │
│        训练步数 [100000]  随机种子 [0]  ☐ 稳定训练            │
├──────────────────────────────────────────────────────────────┤
│  [开始压缩]  [取消]                                           │
│  ████████████████░░░░░░░░  62%   step 62000/100000            │
│  loss 0.000118 · PSNR 39.3 dB · 12.4 ms/step · 剩余 ~7m50s    │
├──────────────────────────────────────────────────────────────┤
│ 质量评估                          [原图|解压|差分] [×1 ×4 ×16]│
│  ┌──────────────┐   逐通道 PSNR                               │
│  │              │    Albedo   41.2 dB  ████████████▌          │
│  │   预览画布    │    Normal   38.7 dB  ███████████            │
│  │              │    Roughness 44.1 dB ██████████████         │
│  └──────────────┘    AO       46.3 dB  ███████████████        │
│  逐 mip PSNR：[41.2 40.8 39.9 37.2 35.1 …]                    │
│  体积：原始 84.0 MB → BCn 21.3 MB → NTC 2.48 MB（33.9×）      │
├──────────────────────────────────────────────────────────────┤
│  [导出 .ntc]  [导出并创建材质]  [加入批量队列]                │
└──────────────────────────────────────────────────────────────┘
```

**实时反馈的实现**：`ITextureSet::RunCompressionSteps()` 是增量的，每次调用跑 `stepsPerIteration` 步后返回 `Incomplete` + `CompressionStats`。在 GDExtension 里开一个压缩线程，每轮把 `stats`（step / loss / ms-per-step）通过 `call_deferred` 发信号给 UI。这正是 `ntc-explorer` 的做法，我们把它搬进 Godot。

目标 PSNR 模式用 `IAdaptiveCompressionSession`：`Reset(targetPsnr, maxBpp)` → 循环 `GetCurrentPreset` / 压缩 / `Next(psnr)` / `Finished()`，UI 上把每轮 (bpp, psnr) 画成散点，最后用 `GetIndexOfFinalRun()` 选中的那次结果。

差分预览用 `MakeImageDifferenceComputePass` + `DecodeImageDifferenceResult`，或直接 CUDA 端 `Decompress` + `ReadChannels` 后在 CPU 上做差。

### 6.3 GDExtension 压缩 API（供 UI 调用）

```cpp
class NtcCompressor : public RefCounted {
public:
    // 能力
    static bool is_cuda_available();
    static Array list_cuda_devices();

    // 构建材质包
    void set_size(int width, int height);
    int  add_texture(const Ref<Image>& img, const String& semantic,
                     const String& channel_swizzle, bool is_srgb,
                     BlockFormat bc_format, float loss_scale);
    void set_mask_channel(int channel, bool discard_masked_out);
    void set_generate_mips(bool);

    // 压缩（异步，跑在自己的线程）
    void start_fixed_bpp(float bpp, int training_steps);
    void start_target_psnr(float psnr, float max_bpp);
    void cancel();
    // 信号: progress(step, total, loss, psnr, ms_per_step)
    //       adaptive_run_finished(run_index, bpp, psnr)
    //       finished(success, error_message)

    // 结果
    Dictionary get_stats() const;       // 逐通道/逐 mip PSNR、体积、latent shape
    Ref<Image> get_decompressed(int texture_index, int mip) const;
    Ref<Image> get_difference(int texture_index, int mip, float gain) const;
    Error save_to_file(const String& path, const Dictionary& lossless_settings);

    // 工具（不需要 CUDA）
    static Dictionary pick_latent_shape(float requested_bpp);   // → {bpp, grid_scale, features}
    static int estimate_size(int w, int h, int channels, int mips, float bpp);
};
```

**无 CUDA 时的兜底**：面板检测到 `is_cuda_available() == false` 时切换到「CLI 模式」，用 `OS.create_process()` 调 `bin/ntc-cli.exe`，通过解析 stdout 给出粗粒度进度。功能降级但不阻塞工作流（例如美术在 AMD 机器上编辑、提交 `.ntcbundle`，由构建机压缩）。

### 6.4 导入管线

两类文件：

**`.ntcbundle`（JSON，我们定义）** — 描述一个待压缩材质包，等价于 NTC 的 `Manifest.json` 加上 Godot 特有字段：

```json
{
  "schema": 1,
  "textures": [
    { "path": "res://tex/PavingStones070_diffuse.png", "name": "Diffuse",
      "semantics": {"Albedo": "RGB"}, "isSRGB": true, "bcFormat": "BC7", "lossFunctionScale": 1.0 },
    { "path": "res://tex/PavingStones070_normal.png", "name": "Normal",
      "semantics": {"Normal": "RGB"}, "bcFormat": "BC7", "lossFunctionScale": 2.0 }
  ],
  "compression": { "mode": "targetPsnr", "targetPsnr": 40.0, "maxBpp": 8.0, "generateMips": true },
  "godot": { "decodeMode": "AUTO", "generateFallbackBCn": true }
}
```

`EditorImportPlugin` 在导入时调用 `NtcCompressor` 生成 `.ntc`（缓存在 `.godot/imported/`，带源文件哈希，避免重复压缩），产物是一个 `NTCTextureSet`。

**`.ntc`（已压缩）** — 直接导入为 `NTCTextureSet`，只读元数据填充属性面板。

> **导出打包的坑**：Godot 默认只打包「被导入过的」文件。`.ntc` 的二进制必须原样进包，做法是让 `EditorImportPlugin._get_save_extension()` 返回 `"res"`，在 `_import()` 里把 `.ntc` 字节流整体写进 `NTCTextureSet` 资源的一个 `PackedByteArray` 属性；或者用 `EditorExportPlugin._export_file()` 显式 `add_file()`。推荐前者：资源自包含，运行时 `IContext::OpenReadOnlyMemory()` 直接从内存流读，省一次文件 IO。

### 6.5 批量转换向导

扫描项目里的 `StandardMaterial3D` / `ORMMaterial3D`，按材质把 albedo / normal / roughness / metallic / AO / emission 槽的纹理聚成包，逐个生成 `.ntcbundle`，排进压缩队列（可选并发度、可选跑在构建机）。完成后提供「一键替换材质引用」并保留原材质备份。

### 6.6 项目设置页

```
rendering/ntc/enabled                        bool     = true
rendering/ntc/decode_mode                    enum     = auto | on_load | on_sample | fallback
rendering/ntc/default_bits_per_pixel         float    = 5.0
rendering/ntc/default_target_psnr            float    = 40.0
rendering/ntc/generate_fallback_bcn          bool     = true
rendering/ntc/on_load/intermediate_pool_mb   int      = 128
rendering/ntc/on_load/use_gdeflate_gpu       bool     = true
rendering/ntc/on_sample/require_taa          bool     = true
rendering/ntc/on_sample/min_gpu_tier         enum     = coopvec | dp4a | any
rendering/ntc/compression/cuda_device        int      = 0
rendering/ntc/compression/use_cli_fallback   bool     = true
```

插件在启用时自动写入 A2 形态需要的 `additional_device_extensions`。

---

## 7. 资产 / 导入 / 导出全流程

### 7.1 作者态

```
美术：PNG/TGA/EXR 贴图
   ↓ 拖进 NTC 压缩面板 / 批量向导
.ntcbundle（进版本控制，文本，可 diff）
   ↓ 导入时压缩（结果按源哈希缓存）
.ntc + NTCTextureSet（+ 可选 fallback .ctex）
```

`.ntcbundle` 进版本控制、`.ntc` 不进（由缓存重建）——对小团队这个策略反过来更省事（`.ntc` 进库，避免每个人本地都要 CUDA）。这个取舍写进项目设置里让团队自选。

### 7.2 运行态（ON_LOAD）

```gdscript
var ts: NTCTextureSet = load("res://materials/paving.ntc")
var mat := StandardMaterial3D.new()
NtcRuntime.apply_to_material(ts, mat)   # 内部按 semantic_map 填槽位
```

`apply_to_material` 内部走 `NtcRuntime.request_decode(ts)`，返回一个带引用计数的 `NtcDecodedSet`，多材质共享。`NTCTextureSet` 被释放时解码产物延迟回收。

### 7.3 运行态（ON_SAMPLE）

```gdscript
var mat := NTCMaterial3D.new()
mat.texture_set = load("res://materials/paving.ntc")
mesh.material_override = mat
```

### 7.4 降级与导出

导出预设里勾选「为不支持的平台生成 BCn 回退」时，`EditorExportPlugin` 在导出期用 `ntc-cli --vk <file>.ntc -i <out> -B auto` 把每个 `.ntc` 转码成 DDS，再转成 Godot `.ctex` 填进 `NTCTextureSet.fallback_textures`。

运行时 `NtcRuntime` 的能力分级：

```
Tier 2: Vulkan + 全部 NTC 特性 + CoopVec           → ON_SAMPLE 可用
Tier 1: Vulkan + 全部 NTC 特性（DP4a）             → ON_LOAD 可用
Tier 0: 其余（Metal / GLES / 特性缺失 / 补丁未装）  → FALLBACK_BCN
```

`AUTO` 模式下按 Tier 自动选择；若资源没有 fallback 且 Tier 0，在编辑器里导出前就报错，不留到运行时崩。

---

## 8. 里程碑与工期

按 1 名熟悉图形 API 的 C++ 工程师 + 0.5 名工具/UI 工程师估算。

| 里程碑 | 内容 | 工期 | 交付判据 |
|---|---|---|---|
| **M0 技术预研** | 在 Godot 里跑通「自建 VkDevice + libntc SPIR-V + 一次 dispatch」；验证 `texture_create_from_extension` 路径 | 1.5 周 | 屏幕上出现一张从 `.ntc` 解出的 BC7 纹理（哪怕是 hardcode 路径） |
| **M1 GDExtension 骨架** | 构建系统（SCons/CMake + godot-cpp）、`NtcRuntime` 单例、能力探测、libntc 链接与 DLL 部署 | 1.5 周 | `NtcRuntime.get_capability_tier()` 在三类机器上返回正确值 |
| **M2 加载时推理 A1** | 元数据解析、latent/权重上传、解压 pass、BC 编码 pass、回读 → `ImageTexture`、mip 链、GDeflate CPU 路径 | 3 周 | `assets/materials` 四个样例材质在 Godot 中渲染，与 `ntc-cli --saveImages` 输出逐像素比对 PSNR > 50 dB |
| **M3 资源与加载管线** | `NTCTextureSet`、`ResourceFormatLoader`、线程化加载、解码缓存/引用计数、中间纹理池、分块解压 | 2 周 | 100 个材质并发流式加载不卡帧、显存峰值受 `intermediate_pool_mb` 约束 |
| **M4 编辑器插件** | 压缩面板、语义槽编辑、进度/PSNR 实时反馈、差分预览、批量向导、导入插件、项目设置、CLI 兜底 | 4 周 | 美术能在不看文档的情况下完成一次「贴图 → .ntc → 场景里生效」 |
| **M5 引擎补丁 P1 + 零拷贝 A3** | Vulkan 特性开启补丁、能力查询 API、零拷贝交付、（可选）A2 外部内存路径 | 2 周 | 去掉回读后加载耗时下降 ≥50%；补丁 PR 提交上游 |
| **M6 采样时推理预研** | `Inference.hlsli` → GLSL（先 DP4a）、glslang 的 `GL_NV_cooperative_vector` 支持验证、必要时 `spirv-link` 备选方案 | 3 周 | 一个独立 compute shader 在 Godot RD 上跑出与 CUDA 参考一致的解码结果 |
| **M7 采样时推理集成** | P2 着色器模板注入、P3 材质 uniform set、P4 内建函数、`NTCMaterial3D`、STF 移植、TAA 联动 | 6 周 | 样例场景在 ON_SAMPLE 模式下显存降低 ≥4×，1080p 帧时间回退在预算内 |
| **M8 打磨与文档** | 降级路径、导出插件、错误处理、性能分析工具（编辑器内 NTC 统计面板）、用户文档 | 2.5 周 | 完整 demo 工程 + 中英文文档 |

**合计约 25.5 周（≈6 个月）**。其中 M0–M4 是「加载时推理 + 压缩插件」的最小可用版本，约 12 周；M5–M8 是采样时推理，约 13.5 周。

强烈建议在 M4 之后做一次**发布**（v0.1，只含加载时推理），先把工具链和资产管线推给团队用起来，再投入风险更高的 M6–M7。

---

## 9. 风险登记

| # | 风险 | 影响 | 概率 | 缓解 |
|---|---|---|---|---|
| R1 | glslang 不支持 `GL_NV_cooperative_vector` | 采样时推理性能不达标（退化成 DP4a） | 中 | M6 优先验证；备选 Slang 离线编译 + `spirv-link`；再不行用 SPIR-V 汇编手动注入 |
| R2 | 引擎补丁与 Godot 版本升级冲突 | 每次升 Godot 都要重新适配 | 高 | 补丁尽量小、尽量分离到 module；P1 推上游；建立自动化 rebase + CI |
| R3 | 跨设备/跨队列同步 bug | 偶发画面撕裂、驱动 hang | 中 | A1 形态用最保守的 fence 全同步；A3 统一走 `call_on_render_thread` |
| R4 | 编辑器压缩需要 CUDA + NVIDIA | 非 N 卡美术无法本地压缩 | 高 | CLI 兜底 + 构建机集中压缩 + `.ntcbundle` 进版本控制的工作流 |
| R5 | NVIDIA 专有许可 | 影响开源/分发 | 高 | 见第 11 节；插件本身与 `libntc` 分离分发 |
| R6 | STF 噪点在 Godot TAA 下不可接受 | 采样时推理画质不达标 | 中 | 先在样例场景做主观评测；alpha/cutout 通道单独走 BC4；必要时接 FSR2 |
| R7 | 中间 float4 纹理导致加载期显存尖峰 | OOM | 中 | 尺寸分档池 + 分块解压（`pSrcRect`）+ 串行化并发解码 |
| R8 | BC7 mode buffer 缺失导致转码慢 20–30× | 加载卡顿 | 低 | 压缩期强制 `--optimizeBC`；导入插件默认勾选 |

---

## 10. 验收标准与测试

### 10.1 正确性

- **黄金基准比对**：对 `assets/materials/*` 与 `assets/testfiles/*`，用 `ntc-cli --vk <f>.ntc -i out/ -B auto` 生成参考 DDS，与 Godot 内解码结果逐像素比对，要求 PSNR > 50 dB（差异只应来自抖动噪声）。
- **跨路径一致性**：A1（回读）与 A3（零拷贝）产物必须逐字节一致。
- **mip 链**：每个 mip 单独比对，覆盖 fused mip 分组边界。
- **格式覆盖**：BC1/3/4/5/6/7 各至少一个测试资产；sRGB 与线性各一；HDR（EXR/HLG）一个。
- **回归**：把上述做成 Godot 的 headless 测试场景，进 CI。

### 10.2 性能

| 指标 | 目标 |
|---|---|
| 2K 五通道材质包加载耗时（A3） | < 4 ms GPU + < 2 ms CPU |
| 加载期显存峰值 | ≤ `intermediate_pool_mb` 设置值 |
| ON_SAMPLE 显存（vs BCn） | ≤ 1/4 |
| ON_SAMPLE 1080p 帧时间回退（RTX 40，全屏 NTC 材质） | 待 M6 基准确定后锁定 |
| 100 材质并发流式加载 | 主线程无 > 2 ms 卡顿 |

### 10.3 工具链

- 美术不看文档，20 分钟内完成首个材质的压缩与应用（可用性测试）。
- 批量转换 200 个材质无人值守跑通。
- 压缩缓存命中时导入耗时 < 100 ms/材质。

---

## 11. 许可与合规

**这一节必须在写第一行代码前由法务确认。**

- `libntc` 及本仓库大部分代码是 **`LicenseRef-NvidiaProprietary`**，受 NVIDIA RTX SDK 许可协议约束。
- 许可**允许**：把 SDK 以目标码形式集成进你的应用分发。
- 许可**禁止**：把 SDK 作为独立产品再分发、逆向、移除声明。
- 修改过的 sample 源码必须保留声明 *"This software contains source code provided by NVIDIA Corporation."*
- **要点**：Godot 本体是 MIT。把 `libntc` 静态链进 Godot 自定义构建并分发该构建，可能被视为「独立分发 SDK」。

**推荐的分发结构**（降低合规风险）：

```
godot-ntc-patch     （MIT，只含 Godot 源码补丁，不含任何 NVIDIA 代码）
addons/ntc          （MIT，GDScript UI 与工具）
ntc_godot 扩展      （我方代码 MIT/自有；动态链接 libntc.dll）
libntc.dll          （NVIDIA 二进制，由用户/发行方按 NVIDIA 许可自行获取与随游戏分发）
```

即：GDExtension **动态**链接 `libntc`，插件仓库不内置 NVIDIA 二进制，改为提供一个「下载/定位 SDK」的安装向导。采样时推理需要把 `Inference.hlsli` 移植成 GLSL——这属于「修改 SDK 材料」，须逐条核对许可第 1/2 节，并保留 NVIDIA 声明。

`RTXTF-Library`（STF）与 `RTXTS-TTM` 各自的许可需单独核对。

---

## 12. 立即可做的下一步（M0 任务清单）

1. 搭一个最小 godot-cpp GDExtension，链接 `bin/windows-x64/libntc.dll`，在 `_ready()` 里调 `ntc::GetLibraryVersion()` 打印版本 —— 半天，验证链接与部署。
2. 用 `get_driver_resource()` 取出 Godot 的 `VkInstance` / `VkPhysicalDevice`，自建一个开满 NTC 特性的 `VkDevice`，`ntc::CreateContext(graphicsApi=Vulkan)` 返回 `Ok` —— 1 天。
3. 加载 `assets/compressed/MetalPlates013.ntc`，`CreateTextureSetMetadataFromStream`，打印纹理数/通道/latent 描述 —— 半天，纯 CPU，无 GPU 依赖。
4. 上传 latent + 权重，`MakeDecompressionComputePass` → 自建管线 → dispatch → 回读 float4 → 存 PNG，与 `ntc-cli` 输出比对 —— 3 天，这是整个项目的关键一跳。
5. 接 `MakeBlockCompressionComputePass` → BC7 → `Image.create_from_data(FORMAT_BPTC_RGBA)` → 贴到一个 `MeshInstance3D` 上 —— 2 天。

第 4 步跑通，说明全部技术假设成立，可以直接进 M1。
