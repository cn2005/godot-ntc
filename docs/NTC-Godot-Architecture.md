# NTC × Godot 架构决策

> 配套文档：[NTC-Godot-Integration-Plan.md](./NTC-Godot-Integration-Plan.md)（可行性、SDK 约束、A1 实测）
> 引擎：Godot 4.7.x Vulkan Forward+
> SDK：RTX NTC SDK v0.10.0 BETA
> 状态：架构 v2（取代落地方案里「一个材质三档 + 改引擎主路径」的设想）

本文只定 **怎么切分所有权**，避免后期 on-sample 把现在的 on-load 路径撑肿。可行性数字、Vulkan 特性清单、压缩管线仍以落地方案为准。

---

## 0. 锁定的决策

1. **`NTCSMaterial3D` 继承 `StandardMaterial3D`，不继承 `BaseMaterial3D`。**
2. **这个类只做「不压缩」和「加载时推理」。** On-sample 是另一种资源，现在不要占位。
3. **`.ntc` 是独立资源 `NTCTextureSet`，不是材质的实现细节。** 解码缓存按 texture set 共享。
4. **引擎默认路径一条都不改。** 现有 `StandardMaterial3D` / Forward+ 模板保持原样。
5. **以后若做 on-sample，只允许加法：新模块 + 新 shader 工厂 + 新材质类。** 禁止改 `BaseMaterial3D::_update_shader`，禁止把 NTC `#include` 进所有材质都走的 `scene_forward_clustered.glsl`。
6. **不要覆盖 `_get_shader_rid`「以备后用」，不要预写一套 PBR shader 生成器。**

继承哪一层解决不了 on-sample。On-sample 卡的是 Godot 着色器编译器和材质描述符布局，不是父类少暴露了什么。

---

## 1. 问题：哪条轴会把架构撑肿

隐式 UX（一个材质、一个枚举、三档）看起来干净，但三档的 **shader 所有者** 不是同一个：

| 档位 | 谁拥有 shader | 运行时数据 | 要不要改引擎 |
|---|---|---|---|
| 不压缩 | 引擎的 `BaseMaterial3D::_update_shader` | 槽位上的普通 `Texture2D` | 否 |
| On load | 同上（只 `set_texture`） | 解码后的 BCn / `Texture2DRD` | 否（A3 只动 device feature） |
| On sample | NTC 自己的 spatial shader | latent array + 权重 SSBO + MLP | 必须（着色语言 / 描述符 / 变体） |

把第三档焊进前两档的类里，后期只能出现这些东西：

- 一个 Resource 两套灵魂（属性校验、序列化、模式切换、编辑器预览全分叉）
- 为了「将来能换 shader」提前覆盖 `_get_shader_rid`，等于开始 fork 材质生成
- 从 `BaseMaterial3D` 往下自己重写 Standard + ORM，把 `material.cpp` 抄进项目

官方提案 [#9494](https://github.com/godotengine/godot-proposals/issues/9494) 已说明：内置材质的检查器和 shader 都在 C++ 里生成，项目侧无法扩展采样路径。更深的继承改变不了这件事。

`BaseMaterial3D` 在 GDExtension 里还是 `is_instantiable: false`。`StandardMaterial3D` / `ORMMaterial3D` 的差别是 C++ 构造函数的 `bool p_orm`，扩展接不住。从它继承 = 自己再实现一遍两种材质。

因此：**不要为了 on-sample 去继承 `BaseMaterial3D`。**

---

## 2. 分层（后面的层可以永远不建）

```
L0  NTCTextureSet + 解码后端
    纯数据与 GPU 解码。和「艺术家看到哪种材质」无关。
    现状：A1（独立 VkDevice + CPU readback）已验证。

L1  NTCSMaterial3D : StandardMaterial3D
    检查器 ≈ 原材质，多一个模式：不压缩 | on load。
    永远用引擎现成 shader，只往槽位塞 Texture2D。
    零引擎改动。

L2  同设备解码（原方案 A3 / P1）
    给 Godot 的 vkCreateDevice 多开 NTC 需要的 feature bit，
    解码结果用 texture_create_from_extension → Texture2DRD 进槽。
    现有材质的采样代码不变。不是 on-sample 的预埋。

L3  加法引擎模块 modules/ntc（可选，以后）
    新 render_mode、新 ntc_sample() 内建、新 uniform set。
    只有选了该 mode 的 shader 才编译 NTC 变体。
    不改 material.cpp 的默认生成，不改未使用 NTC 的 Forward+ 路径。

L4  NTCSampleMaterial3D : Material（可选，以后）
    使用 L3 的 shader 工厂。与 L1 并列，不是 L1 的第三档。
```

依赖只能向下：L1 依赖 L0，L2 强化 L0，L4 依赖 L3。**L1 的公开 API 不得出现 L3/L4 的类型或枚举。** 不做 L3/L4 时，L1 一行都不用改。

```
        编辑器 / 构建机（CUDA 压缩）
                    ↓ .ntc
              ┌─────────────┐
              │ NTCTextureSet│  L0  数据
              └──────┬───────┘
                     │ 引用（可共享）
         ┌───────────┴────────────┐
         │                        │
         ▼                        ▼
  NTCSMaterial3D            NTCSampleMaterial3D
  L1 Standard 子类          L4 新 Material（未建）
  none | on_load            on sample
  引擎 shader               模块 shader
         │                        │
         ▼                        ▼
   set_texture()             ntc_sample()
   BCn / Texture2DRD         latent + MLP
         │                        │
         └────────┬───────────────┘
                  ▼
            L0 解码后端
            A1 现有 │ L2 同设备（A3）
```

---

## 3. 资源模型

### 3.1 `NTCTextureSet`（L0，已有雏形）

对应一个 `.ntc`（一份材质的多通道神经表示）。它 **不管** 用哪种方式呈现。

职责：

- 指向 `.ntc` 字节 / 路径
- 语义映射（albedo / normal / roughness / … → 通道）
- 只读元数据：宽高、mips、bpp、是否 GenericInt8
- 可选：不支持平台用的预烘焙 BCn（`fallback_textures`）

不职责：

- 不是 Material，不出现在 Mesh 的 material 槽
- 不持有「当前是 on load 还是 on sample」——那是材质类的选择
- 不在资源里复制解码后的 GPU 图像；缓存放 `NtcRuntime`

多个 `NTCSMaterial3D`（以及将来的 sample 材质）可以引用同一份 `NTCTextureSet`，解码一次。

### 3.2 `NTCSMaterial3D`（L1，下一步）

```
NTCSMaterial3D : StandardMaterial3D
    ntc_mode          NONE | ON_LOAD
    ntc_texture_set   Ref<NTCTextureSet>   // 可隐藏，检查器只露 mode
```

| `ntc_mode` | 行为 |
|---|---|
| `NONE` | 父类原样。槽位贴图直接进引擎 shader。 |
| `ON_LOAD` | 向 `NtcRuntime` 要该 set 的 BCn（或 `Texture2DRD`），`set_texture()` 填槽，打开对应 feature（法线、ORM 等）。 |

硬约束：

- **不覆盖** `_get_shader_rid` / `_get_shader_mode`。画什么由父类决定。
- **不生成** Godot shader 文本。
- 从 `ON_LOAD` 切回 `NONE` 必须还能回到原始 `Texture2D` 引用，因此源图与运行时解码图要分开存。
- 编辑器保留源图以便改通道 / 改模式；导出才丢掉 PNG，包体收益才成立。
- `on_sample` **不是** 这个 enum 的合法值。

检查器可以做到「只多一个选项」：`ntc_texture_set` 用 `_validate_property` 藏起来，或由导入器写入。没有这份数据，`ON_LOAD` 没有输入，不能假装 enum 是唯一状态。

### 3.3 `NTCSampleMaterial3D`（L4，现在不建）

`Material` 的兄弟类型，**不是** `StandardMaterial3D` / `BaseMaterial3D` 的子类。

将来才需要存在的理由：STF、TAA 强制、CoopVec 变体、额外 descriptor set、opacity 走独立 BC4——这些都不该进 L1。

美术侧若仍想像「一个入口」，由 **编辑器插件按模式创建不同资源**，而不是一个对象内部 `if (mode)`。检查器可以长得像，类型必须分开。

### 3.4 ORM

`ORMMaterial3D` 与 `StandardMaterial3D` 是兄弟，不是父子。L1 只包 Standard。若以后要 ORM，再加 `NTCSORMMaterial3D : ORMMaterial3D`，一行包装，不预先抽象到 `BaseMaterial3D`。

---

## 4. 引擎改动政策

「不魔改引擎原有功能」落成一条规则：**只做加法，默认路径零改动。**

| 允许 | 禁止 |
|---|---|
| `vkCreateDevice` 多开 `storageBuffer8BitAccess` / `shaderIntegerDotProduct` / `shaderDemoteToHelperInvocation` 等（L2） | 改所有材质都走的 `texture()` 采样 |
| 新模块 `modules/ntc`，新 `render_mode`，新 `ntc_sample()`（L3） | 改 `BaseMaterial3D::_update_shader` 给全材质加 NTC 分支 |
| 新材质存储数据，只绑到 NTC sample 材质（L3） | 把 NTC 无条件 `#include` 进 `scene_forward_clustered.glsl` |
| 现有 shader 不引用的新内建函数 | 为了「一个枚举三档」去改 ClassDB 里的 Standard 行为 |

L2 打开的 feature bit，现有 SPIR-V 不会用到，不改变 `StandardMaterial3D` 的生成代码。这是 on-load 的产品化，不是 on-sample 预埋。

原落地方案 5.2 的「注入主模板 + 全局变体宏」**降级为反模式**。即使加 `NTC_INFERENCE` 宏，也是在改引擎主路径，升 Godot 必打架。L3 若开工，NTC 变体只从模块的 shader 工厂产出。

Godot 着色语言没有 SSBO、没有 `dot4add`、不能挂自定义 descriptor set，MLP 无法用 `.gdshader` 手写。因此 **不存在「纯 GDExtension 的 on-sample」**。承认这一点，比在 L1 里留一个灰掉的第三档更安全。

---

## 5. 解码后端（L0 / L2）

与材质类解耦。`NtcRuntime` 对同一 `NTCTextureSet` 只解码一次，返回可塞进 `Texture2D` 槽的句柄。

| 后端 | 设备 | 结果怎么进 Godot | 何时用 |
|---|---|---|---|
| A1（已落地） | 扩展自建 `VkDevice` | readback → `Image` / `ImageTexture` | 原版 Godot，验证与编辑器 |
| L2 / A3 | Godot 自己的 `VkDevice` | `texture_create_from_extension` → `Texture2DRD` | 运行时 on-load，免主机往返 |

L1 只依赖「给我一组按语义排好的 `Texture2D`」，不依赖图像在哪个 device 上创建。切换 A1 → A3 不应改 `NTCSMaterial3D` 的属性表。

平台不支持时走 `NTCTextureSet.fallback_textures`，L1 仍是 `set_texture()`，模式语义不变。

---

## 6. 明确不做（防止预埋）

现在 **不要**：

- 给 `NTCSMaterial3D` 加 `ON_SAMPLE` 枚举值或灰掉的第三档
- 覆盖 `Material::_get_shader_rid` 留钩子
- 在扩展里复制 `BaseMaterial3D` 的 shader 字符串拼接
- 继承 `BaseMaterial3D` 做「将来 Standard + ORM + NTC 一张网」
- 一个门面类内部按 mode 换两套实现（等 L4 真的存在再谈门面，现在谈就是挖坑）
- 把 per-frame compute 解码伪装成 on-sample（那是 on-load 每帧重做，或 feedback 流送，另开设计）

以后 **可以** 再开、且不必回头改 L1：

- `modules/ntc` + `ntc_sample()` + `NTCSampleMaterial3D`
- 编辑器「从 NTCSMaterial3D 转为 sample 材质」的转换器
- ORM 包装类
- Inference on Feedback / 稀疏瓦片（Godot 无 sampler feedback，仍列 v2）

---

## 7. 和落地方案 v1 的对照

| v1 设想 | v2 |
|---|---|
| 一种 `NTCMaterial3D` 覆盖 on-sample，on-load 则挂到普通 Standard | 拆成 L1（Standard 子类，on-load）与 L4（新 Material，sample） |
| 隐式三档做进同一个检查器 | L1 最多两档；sample 是另一种资源 |
| P2 注入 `scene_forward_clustered.glsl` | 禁止改主路径；L3 用模块自己的 shader 工厂 |
| P3/P4 扩材质存储与着色语言 | 仍需要，但只服务 L4，且必须是加法 |
| P1 设备特性 | 保留，作为 L2，服务 on-load 零拷贝 |
| `NTCTextureSet.decode_mode` 含 ON_SAMPLE | 解码模式不属于 texture set；呈现方式属于材质类 |

落地方案第 2 节的约束仍然成立：Godot SL 表达不了采样时推理、主设备缺 feature bit、跨 RD 可用 `texture_create_from_extension`。变的是 **产品切分和引擎接触面**，不是那些事实。

---

## 8. 当前落地顺序

1. **稳住 L0**：`NTCTextureSet` 只做数据 + 语义；`NtcRuntime` 缓存解码结果。
2. **做 L1**：`NTCSMaterial3D`，`NONE` / `ON_LOAD`，内部调用已有 A1 填槽。隐式 UX 在这一层完成。
3. **做 L2**：引擎 **加法** 打开 device feature，同设备转码，L1 无感知换成 `Texture2DRD`。
4. **停。** L3/L4 单独立项，预研仍按落地方案 M6（CoopVec / glslang / SPIR-V link），通过后再建模块与新类。

编辑器压缩面板（落地方案 G3）产出 `NTCTextureSet`，不产出「带三档的超级材质」。
