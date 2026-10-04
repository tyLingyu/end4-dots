# ii-shell

illogical-impulse（`dots/.config/quickshell/ii`）的原生 C++ 移植，目标是用 GLES 渲染、只支持 Hyprland，外观与 QML 版尽量 1:1。

当前处于**阶段 0（脚手架）**：只验证 Wayland + GLES 渲染链路，在每个输出顶部画一个带文字的圆角面板。

## 构建

依赖（Arch）：`meson wayland wayland-protocols mesa libxkbcommon cairo pango harfbuzz freetype2 fontconfig librsvg libwebp libjxl stb`

```sh
meson setup build
ninja -C build
./build/ii-shell
```

开发构建默认开启 AddressSanitizer + UBSan；发布构建请用 `meson setup build --buildtype=release -Db_sanitize=none`。

## 目录

| 路径 | 内容 |
|---|---|
| `src/core` `src/util` `src/render` `src/wayland` `src/system` `src/i18n` | 拷自 Noctalia（见下），按需裁剪 |
| `src/app/main_loop.*` | 拷自 Noctalia，去掉了与其 Bar / Application 的耦合 |
| `src/main.cpp` | ii-shell 入口（阶段 0 为演示面板） |
| `protocols/` | 非 wayland-protocols 自带的协议 XML（拷自 Noctalia） |
| `third_party/wuffs` | 图片解码，许可证见目录内 |

## 第三方代码声明

`src/` 下除 `main.cpp` 外的大部分代码，以及 `protocols/`、`third_party/` 拷贝自
[Noctalia](https://github.com/noctalia-dev/noctalia) commit `e639a87`（v5.2.1），以 MIT 许可证发布：

> Copyright (c) 2026 noctalia-dev

完整许可证文本见 [`LICENSE.noctalia`](LICENSE.noctalia)。拷贝后不再跟踪上游。对拷贝代码的改写在源码中以 `ii-shell:` 注释标出；
整段删除的部分为：其他合成器（niri / sway / KDE / dwl / mango / umbriel）的支持与合成器抽象层、剪贴板服务、
Tabler 图标字体与其字形注册表、可配置的 Tab 焦点快捷键、构建版本信息。
