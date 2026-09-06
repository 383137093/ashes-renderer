# Ashes Renderer

[English](README.md) | [简体中文](README.zh-CN.md)

Ashes Renderer 是一个基于 **C++17** 的实时 CPU 软光栅渲染器，专注于在纯 CPU 环境中实现完整的现代渲染管线。

它支持 PBR / IBL、方向光阴影、骨骼动画、多种抗锯齿与透明渲染，并通过逐三角形并行光栅化、多级剔除、Early-Z 和 Reversed-Z 优化 CPU 渲染性能。项目提供 Windows / macOS / Linux 原生窗口实现，渲染器核心仅依赖 `nlohmann/json` 和 `stb_image` 两个头文件级第三方库。

![Ashes Renderer 主效果图](Docs/Images/MechDrone.gif)

## 目录

- [功能概览](#功能概览)
- [渲染管线](#渲染管线)
  - [阶段职责](#阶段职责)
  - [几何处理与光栅化](#几何处理与光栅化)
  - [材质与光照](#材质与光照)
  - [抗锯齿、透明与颜色](#抗锯齿透明与颜色)
  - [性能设计](#性能设计)
- [资产导入与动画](#资产导入与动画)
- [运行时交互](#运行时交互)
- [构建、运行与跨平台](#构建运行与跨平台)
- [IBL 环境预计算](#ibl-环境预计算)
- [第三方依赖](#第三方依赖)
- [项目结构](#项目结构)
- [License](#license)

## 功能概览

- **CPU 渲染管线**：Depth Pre-pass、Shadow Pass、Base / Transparent Shading Pass 按阶段执行。几何阶段进行齐次空间裁剪，光栅化阶段使用扫描线生成片元，并进行透视校正插值；深度处理采用 Reversed-Z。
- **材质与光照**：支持 Blinn-Phong、PBR Metallic-Roughness、PBR Specular-Glossiness。PBR 材质支持基于 HDR 环境贴图的 IBL；实时灯光包含环境光、方向光和点光源，并支持实时方向光阴影，可选择硬阴影或 PCF 过滤。
- **抗锯齿、透明与颜色**：支持 MSAA、SSAA、ECSAA、Alpha Blending 和固定 4 层 OIT。后处理包含自动曝光、ACES Filmic Tone Mapping 和 Gamma 校正。
- **性能设计**：使用自研 `ThreadPool` 和分段 `SpinLock` 实现逐三角形并行光栅化。通过视锥 / 包围盒剔除、背面剔除、齐次空间裁剪、早期深度测试、渲染顺序优化和底层热点优化减少无效计算。
- **数学基础**：自研 `Vector`、`Matrix`、`Quaternion`、`TransformationSRT`，支撑几何变换、相机与投影、裁剪与剔除、骨骼蒙皮和动画插值。
- **资产导入与动画**：支持 glTF 2.0、OBJ / MTL 和 `.ashes` 场景配置。glTF 导入覆盖网格、材质、纹理、相机、节点层级、Punctual Lights、骨骼蒙皮、关键帧动画和两类 PBR 材质工作流。
- **跨平台工程**：提供 Windows、macOS、Linux 原生窗口实现，并通过原生菜单提供运行时配置入口。项目包含各平台的工程或构建入口，渲染器核心仅依赖 `nlohmann/json` 和 `stb_image` 两个头文件级第三方库。

## 渲染管线

每帧按以下阶段执行：

```text
场景更新与动画
  -> Draw Call 收集与视锥剔除
  -> Depth Pre-pass
  -> Shadow Pass
  -> Base / Transparent Shading Pass
  -> Screen Pass
  -> 原生窗口显示
```

### 阶段职责

- **Depth Pre-pass**：不透明物体先写入深度，再进入完整着色阶段，减少被遮挡片元的材质计算。
- **Shadow Pass**：为阴影投射光源生成阴影贴图，供后续着色阶段采样。
- **Base / Transparent Shading Pass**：先绘制不透明物体，再绘制透明物体。透明物体按视图深度从远到近排序，以保证 Alpha Blending 的合成顺序。
- **Screen Pass**：完成 ECSAA / OIT 合成、MSAA / SSAA 降采样、自动曝光、ACES Filmic Tone Mapping、Gamma 校正和背景填充。

### 几何处理与光栅化

- **齐次空间裁剪**：在透视除法前对视锥六个平面执行多边形裁剪，正确处理穿过近平面或屏幕边界的三角形。
- **反向深度（Reversed-Z）**：近裁剪面映射为 `1.0`，远裁剪面映射为 `0.0`，提高远距离深度精度。
- **扫描线光栅化**：按每条扫描线求三角形三条边的交点范围，逐行生成待着色像素跨度。
- **重心坐标与透视校正插值**：基于顶点倒数深度修正屏幕空间重心权重，用于插值纹理坐标、法线和世界空间属性，避免普通重心插值在透视投影下造成纹理形变或光照偏差。
- **纹理梯度**：由屏幕空间与纹理空间的微分面积比值求出纹理坐标变化率，供 Mipmap LOD 选择使用。

### 材质与光照

渲染器包含三套材质工作流：

- **Blinn-Phong**：支持环境项、漫反射、高光和法线贴图，主要用于 OBJ / MTL 资产。
- **PBR Metallic-Roughness**：支持 Base Color、Metallic、Roughness、Normal、Occlusion 和 Emissive 通道。
- **PBR Specular-Glossiness**：支持 Albedo、Specular、Glossiness、Normal、Occlusion 和 Emissive 通道。

IBL 使用预计算的 **irradiance cubemap、prefilter cubemap 和 BRDF LUT**，为 PBR 材质提供漫反射环境光与随粗糙度变化的镜面反射。实时灯光包含环境光、方向光和点光源；当前阴影仅支持方向光，可选择硬阴影或 PCF 过滤，并可调节阴影贴图分辨率。

![PBR 材质参考模型的 IBL 渲染效果](Docs/Images/PbrMaterialReference.png)

### 抗锯齿、透明与颜色

- **MSAA / SSAA**：分别通过多重采样和 2x 超采样提升边缘质量；两者互斥，不可同时开启。
- **ECSAA**：自研低开销三角形边缘抗锯齿，在片元覆盖边界处估算覆盖率并混合颜色，可独立开启，也可与 MSAA 或 SSAA 组合。
- **透明渲染**：支持常规 Alpha Blending，也支持按深度排序合成的 OIT。OIT 每像素最多保留 **4 层**透明采样（固定容量，非链表），超出部分会被丢弃，适合轻中度重叠的透明场景。
- **颜色处理**：支持基于画面平均亮度的自动曝光、ACES Filmic Tone Mapping 和 Gamma 校正。

抗锯齿效果对比：

<table>
  <tr>
    <th width="50%">无抗锯齿</th>
    <th width="50%">ECSAA</th>
  </tr>
  <tr>
    <td width="50%"><img src="Docs/Images/MonRonera.png" alt="MonRonera 无抗锯齿效果" width="100%"></td>
    <td width="50%"><img src="Docs/Images/MonRoneraECSAA.png" alt="MonRonera ECSAA 抗锯齿效果" width="100%"></td>
  </tr>
  <tr>
    <th width="50%">MSAA</th>
    <th width="50%">SSAA</th>
  </tr>
  <tr>
    <td width="50%"><img src="Docs/Images/MonRoneraMSAA.png" alt="MonRonera MSAA 抗锯齿效果" width="100%"></td>
    <td width="50%"><img src="Docs/Images/MonRoneraSSAA.png" alt="MonRonera SSAA 抗锯齿效果" width="100%"></td>
  </tr>
</table>

OIT 透明渲染效果对比：

<table>
  <tr>
    <th width="50%">Alpha Blending</th>
    <th width="50%">OIT</th>
  </tr>
  <tr>
    <td width="50%"><img src="Docs/Images/BusterDrone.gif" alt="BusterDrone Alpha Blending 透明渲染效果" width="100%"></td>
    <td width="50%"><img src="Docs/Images/BusterDroneOIT.gif" alt="BusterDrone OIT 透明渲染效果" width="100%"></td>
  </tr>
</table>

纹理过滤效果对比：

<table>
  <tr>
    <th width="50%">无纹理过滤</th>
    <th width="50%">LinearMipPoint</th>
  </tr>
  <tr>
    <td width="50%"><img src="Docs/Images/Sponza.png" alt="Sponza 无纹理过滤效果" width="100%"></td>
    <td width="50%"><img src="Docs/Images/SponzaLinearMipPoint.png" alt="Sponza LinearMipPoint 纹理过滤效果" width="100%"></td>
  </tr>
</table>

### 性能设计

Ashes Renderer 在多个粒度上叠加剔除与并行手段，以在纯 CPU 环境下获得较高的吞吐：

- **多级剔除**：Draw Call 级的视锥 / 包围盒剔除 → 三角形级的背面剔除与齐次空间裁剪 → 像素批级的三角形最小深度早期测试（先用三角形内最接近相机的深度值与帧缓冲已有深度快速比较，未通过的整批像素直接跳过重心插值与着色）。
- **渲染顺序优化**：不透明物体按视图深度从近到远渲染，优化 Depth Pre-pass 中的深度缓冲构建；随后 Base Shading Pass 利用该深度缓冲减少被遮挡片元的着色。透明物体则从远到近渲染，保证 Alpha Blending 的合成顺序正确。
- **底层热点优化**：数学与纹理采样路径尽量减少运行时开销，包括 `FastLog2` / `FastPow2` 近似公式、纹理 LOD 小数转换预计算表、全局静态查表数据、模板化纹理过滤分派，以及大量 `constexpr` 计算和模板化的向量、矩阵、插值工具。
- **细粒度并行**：Draw Call 与三角形均由 `ThreadPool` 以原子计数动态领取，多个线程可同时处理同一 Draw Call 内的不同三角形；帧缓冲使用分段 `SpinLock` 而非全局互斥量，将写入竞争限制在极小范围内。

## 资产导入与动画

### glTF 2.0

支持网格、材质、纹理、相机、节点层级、Punctual Lights、骨骼蒙皮与关键帧动画，并覆盖 Metallic-Roughness 和 Specular-Glossiness 两类 PBR 材质工作流。

### OBJ / MTL

支持静态网格及 MTL 材质，适合快速检查传统模型资产。

### `.ashes` 场景配置

加载 glTF 或 OBJ 时，渲染器会查找同名 `.ashes` 文件。该文件可追加相机和灯光，并预设常用运行时渲染配置。

## 运行时交互

原生菜单可在不重新编译或重启程序的情况下调整：

- **File / Pause**：打开或切换模型，暂停渲染循环，退出程序；
- **Camera / Animation / Envir**：切换相机、动画 / Movie 和 IBL 环境；
- **Mode**：查看最终颜色、深度、法线及各材质通道调试视图；
- **Quality**：调整纹理过滤、阴影质量和抗锯齿模式；
- **Misc**：调整渲染线程数、背景颜色、地面、背面剔除、OIT、ACES 与 Gamma 校正。

鼠标左键拖动用于旋转视角，右键拖动用于平移，滚轮用于拉近或拉远。窗口标题实时显示 FPS 和帧耗时。

![Windows 平台菜单图](Docs/Images/MenuWindows.png)

![macOS 平台菜单图](Docs/Images/MenuMac.png)

![Linux 平台菜单图](Docs/Images/MenuLinux.png)

## 构建、运行与跨平台

项目要求编译器和标准库完整支持 **C++17**。各平台都提供 Debug / Release 两种编译配置。

| 平台 | 项目文件 / 构建入口 | 工具链要求 |
|------|----------------------|------------|
| Windows | [Build/Windows/AshesRenderer.sln](Build/Windows/AshesRenderer.sln) | Visual Studio 2019 或更高版本，MSVC v142 工具集，Windows 10 SDK。 |
| macOS | [Build/Mac/AshesRenderer.xcodeproj](Build/Mac/AshesRenderer.xcodeproj) | Xcode 16.2 或更高版本，Apple Clang，C++17，macOS 15.2 SDK；窗口层依赖 AppKit。 |
| Linux | [Build/Linux/Makefile](Build/Linux/Makefile)，也可用 [Build/Linux/AshesRenderer.code-workspace](Build/Linux/AshesRenderer.code-workspace) 打开工程 | GCC 11 或更高版本，`make`，X11 运行库与开发头文件（链接 `libX11`）。 |

程序默认加载：

```text
Resources/Models/gltf/pbr_material_reference/scene.gltf
```

也可以通过运行时的 **File > Open** 菜单加载其他 `.gltf` 或 `.obj` 文件。

## IBL 环境预计算

[Tools/IBLCubemapConverter](Tools/IBLCubemapConverter) 提供 Python 工具，用于将 HDR 全景图转换为渲染器所需的 irradiance cubemap、prefilter cubemap 和 BRDF LUT。安装依赖：

```bash
python -m pip install -r Tools/IBLCubemapConverter/requirements.txt
```

工具的入口与参数定义位于 [Tools/IBLCubemapConverter/main.py](Tools/IBLCubemapConverter/main.py)。生成结果可放入 `Resources/Envirs/<环境名称>/`，随后通过 **Envir** 菜单切换。

## 第三方依赖

渲染器核心仅依赖标准库，以下第三方代码内嵌于 [Source/ThirdParty](Source/ThirdParty)：

| 库 | 用途 | 许可证 |
| --- | --- | --- |
| [nlohmann/json](https://github.com/nlohmann/json) (`json.hpp`, v3.10.4) | 解析 glTF 与 `.ashes` 场景配置 | MIT |
| [stb_image](https://github.com/nothings/stb) (`stb_image.h`, v2.27) | 加载 PNG / JPG / HDR 等纹理与环境贴图 | Public Domain / MIT |

[Tools/IBLCubemapConverter](Tools/IBLCubemapConverter) 的 Python 依赖版本见其 [requirements.txt](Tools/IBLCubemapConverter/requirements.txt)。

## 项目结构

```text
Source/
|-- Asset/       # 网格、材质、纹理、相机、灯光、骨骼与动画
|-- Geometry/    # 包围盒、视锥体、裁剪与三角形几何
|-- Math/        # 向量、矩阵、四元数与变换
|-- Parser/      # glTF、OBJ、MTL 与 .ashes 解析
|-- Platform/    # Windows / macOS / Linux 原生窗口层
|-- Rasterize/   # 渲染器、帧缓冲、Shader、场景与应用入口
`-- Utility/     # 线程池、锁与通用工具

Resources/       # 示例模型与 HDR / IBL 环境
Test/            # 数学模块单元测试
Tools/           # 离线资源处理工具
Build/           # 各平台工程文件
```

## License

本项目基于 [MIT License](LICENSE) 发布。
