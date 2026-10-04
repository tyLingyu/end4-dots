# qml2cpp 数据

## `var-types.json`

ii 中全部 292 个 `property var`（`extract_vars.py` 抽取，源码基于 `e595b6ae`）对应的 C++ 类型，供 qml2cpp 生成属性声明时使用。

- `structs`：推断出的 56 个结构体（字段名 → 类型）。
- `properties`：每个属性的 `file` / `line` / `name` / `cpp_type`，以及 `kind`、`confidence`（high 274 / medium 15 / low 3）、`evidence`、`notes`。

类型由子代理逐个阅读源码与引用推断，再统一合并：同形结构合并为一个名字，同名不同形的取字段并集。
`confidence` 不是 `high` 的条目在翻译到对应文件时需人工确认。

### 类型约定

| 情况 | 写法 |
|---|---|
| 基本类型 | `bool` / `int` / `double` / `std::string` |
| 颜色、几何、时间 | `ii::Color`、`ii::Point` / `ii::Size` / `ii::Rect`、`ii::DateTime` |
| 列表、可空、映射 | `std::vector<T>`、`std::optional<T>`、`std::map<K, T>` |
| 本项目组件实例 | `组件名*`（QML 文件名；文件内的 inline component 同样用其名字，如 `Notif*`、`BrightnessMonitor*`） |
| Qt Quick 内置类型实例 | `ii::Item*`、`ii::Text*`、`ii::Timer*` 等 |
| Quickshell 对象 | `qs::<类型名>*`，如 `qs::PwNode*`、`qs::MprisPlayer*` |
| 回调 | `std::function<R(Args...)>` |
| QML 文件里声明的 enum | 嵌套在该组件类里：`RoundCorner::CornerEnum`、`StateLayer::State` |
| Qt / Quickshell 的 enum | `ii::Easing::Type`、`ii::MouseButton`、`qs::UPowerDeviceState`、`qs::Edges` 等 |
| 字段名与 C++ 关键字冲突 | 加下划线后缀：`class_`、`namespace_`（JSON 键名不变） |
| 无固定结构的 JSON | `nlohmann::json`（共 6 处，见下） |

### 仍为 `nlohmann::json` 的 6 处

| 属性 | 原因 |
|---|---|
| `GCloudApi.outputData` | 同时承载 Vision 与 Translate 两种响应 |
| `GCloudVisionResult.rawData` | 原样保存的响应，无读取方，移植时可删 |
| `HyprlandConfigOption.value` | `hyprctl getoption -j` 的值类型随选项变化 |
| `ConfigSelectionArray.currentValue` | 视所编辑的配置项为 bool / int / string |
| `GoogleCloud.keyContent` | 用户粘贴的服务账号密钥，原样转交脚本 |
| `KeyringStorage.keyringData` | 调用方按任意路径写入 |

### 推断中发现的问题

- `SelectionDialog`、`WeekRow` 在整个仓库中没有被使用（对应 3 条 low 置信度）。
- `Weather.location` 初始化声明了 `lon`，但读写都用 `.long`，结构体按 `lon` 统一。
- `Images.thumbnailSizes` 依赖对象键的插入顺序，换成 `std::map` 后需按尺寸遍历。
