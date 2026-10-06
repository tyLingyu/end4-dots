# ii-shell

illogical-impulse（`dots/.config/quickshell/ii`）的原生 C++ 移植，目标是用 GLES 渲染、只支持 Hyprland，外观与 QML 版尽量 1:1。

当前处于**阶段 2（翻译器）**：QML 的属性 / 绑定、Item 树、anchors、布局、Text、动画与 Behavior 已在 C++ 中实现并对 Qt 校准（阶段 1）；
[`tools/qml2cpp`](../tools/qml2cpp) 把 ii 的 QML 翻译成使用这套运行时的 C++（生成到 `src/ii/`，尚未接入构建，等阶段 3 的 Quickshell 兼容层）。
`ii-shell` 目前在每个输出中央显示 `tests/visual/osd_demo.qml` 的 C++ 版本作为端到端检查，`ii-shell --animate` 让音量循环变化。

## 构建

依赖（Arch）：`meson wayland wayland-protocols mesa libxkbcommon cairo pango harfbuzz freetype2 fontconfig librsvg libwebp libjxl stb`

```sh
meson setup build
ninja -C build
./build/ii-shell
```

开发构建默认开启 AddressSanitizer + UBSan；发布构建请用 `meson setup build --buildtype=release -Db_sanitize=none`。

## 测试

```sh
meson test -C build
```

| 测试 | 内容 |
|---|---|
| `tests/runtime/property_test.cpp` | 属性与绑定语义（依赖追踪、赋值打断绑定、循环、各种销毁时序） |
| `tests/runtime/item_test.cpp` | Item 树的运行期变化（anchors 增删、重设父项、布局随子项变化重排） |
| `tests/runtime/easing_test.cpp` | 缓动曲线与 Qt 的采样值比对（`tests/diff/easing_dump.qml` 经 AnimationController 导出） |
| `tests/runtime/color_test.cpp` | QML 颜色字面量与 `Qt.hsva` / HSL 访问器（`tests/diff/color_dump.qml`） |
| `tests/runtime/js_test.cpp` | `runtime/js.h` 的 JS 语义与 Qt 的 V4 引擎逐例比对（`tests/diff/js_dump.qml`，1709 例） |
| `tests/runtime/animation_test.cpp` | 动画、组合动画、Behavior（手动时钟） |
| `tests/diff/` | **与 Qt 的差分测试**：`cases/*.qml` 由 Qt 运行并导出几何（`regen.py` → `expected/*.json`），`diff_test.cpp` 用运行时构建同样的场景比对 |
| `tests/visual/compare.sh` | 视觉对比：Qt 渲染 `osd_demo.qml` 与 ii-shell 实机截图逐像素比较（需在 Hyprland 下运行） |

`expected/` 生成于 KDE 应用字体为 Google Sans Flex 11pt、字重 500 的环境；新增或修改用例（含 `*_dump.qml`）后运行 `tests/diff/regen.py`（需要 `qml6`）。

## 目录

| 路径 | 内容 |
|---|---|
| `src/core` `src/util` `src/render` `src/wayland` `src/system` `src/i18n` | 拷自 Noctalia（见下），按需裁剪 |
| `src/app/main_loop.*` | 拷自 Noctalia，去掉了与其 Bar / Application 的耦合 |
| `src/runtime` | QML 运行时：`property`（绑定）、`object` / `item`、`anchors`、`layout`（Row/ColumnLayout）、`positioner`（Row/Column）、`rectangle`、`text` / `text_layout`、`color`、`easing`、`animation`（驱动器、Number/Color/Rotation/SmoothedAnimation、Sequential/Parallel、Behavior）、`component`（Component 工厂）、`js`（生成代码用的 JS 语义） |
| `src/ii` | qml2cpp 的生成物（不进版本库，见 `tools/qml2cpp`） |
| `src/core/lsan_suppressions.cpp` | ASan 构建下屏蔽 fontconfig / Pango 字体缓存的误报 |
| `src/main.cpp` | ii-shell 入口（当前为阶段 1 演示） |
| `protocols/` | 非 wayland-protocols 自带的协议 XML（拷自 Noctalia） |
| `third_party/wuffs` | 图片解码，许可证见目录内 |

## 第三方代码声明

`src/` 下除 `main.cpp`、`runtime/` 外的大部分代码，以及 `protocols/`、`third_party/` 拷贝自
[Noctalia](https://github.com/noctalia-dev/noctalia) commit `e639a87`（v5.2.1），以 MIT 许可证发布：

> Copyright (c) 2026 noctalia-dev

完整许可证文本见 [`LICENSE.noctalia`](LICENSE.noctalia)。拷贝后不再跟踪上游。对拷贝代码的改写在源码中以 `ii-shell:` 注释标出；
整段删除的部分为：其他合成器（niri / sway / KDE / dwl / mango / umbriel）的支持与合成器抽象层、剪贴板服务、
Tabler 图标字体与其字形注册表、可配置的 Tab 焦点快捷键、构建版本信息。文字渲染改为不取整字形位置、关闭 hint metrics，
并支持在字体族名后以 `@axis=value` 传入字体变体，以与 Qt 的排版一致。
