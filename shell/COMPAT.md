# 阶段 3 待实现清单

qml2cpp 为 OSD 及其依赖（24 个 QML 文件）生成的代码引用了下面这些东西，但它们还不存在。清单来自生成代码的 include、各文件的 JS 译文，以及填写译文时的报告。API 的命名约定见 [`tools/qml2cpp/FILL.md`](../tools/qml2cpp/FILL.md)，概括如下：

- 属性是 `Property<T>`，信号是 `Signal<...>`，方法与 QML 同名。
- 保留字加 `_` 后缀；属性与信号同名时，信号加 `Signal` 后缀。
- Quickshell 类型在 `ii::qs`，与 `.qmltypes` 逐一对应。

## 运行时（`src/runtime`）

| 头文件 | 内容 |
|---|---|
| `geometry.h` | `Point`、`Size`、`Rect`（值类型，带 `operator==`） |
| `datetime.h` | `DateTime` |
| `timer.h` | `Timer`：`interval`、`running`、`repeat`、`triggeredOnStart`、`triggered`、`start/stop/restart`。Qt 的行为：`triggeredOnStart` 加不重复的计时器会触发两次，启动时一次，到期一次 |
| `connections.h` | `Connections`：`target`（`Property<Object*>`）、`setConnector(fn(Object*, std::vector<Connection>&))`，target 变化时重连，target 为空时不连 |
| `loader.h` | `Loader`：`active`、`sourceComponent`（`Component<Object>`）、`source`（相对 QML 文件的 URL，需要生成器提供 URL 到工厂的注册表）、`item` |
| `mouse_area.h` | `MouseArea`：`hoverEnabled`、`entered`，`pressed` 属性和 `pressedSignal` 信号 |
| `canvas.h` | `Canvas`：`getContext("2d")`（跨帧保留状态，不支持的 id 返回 nullptr）、`requestPaint()`、`Signal<const Rect&> paint`。`Context2D`：`clearRect`、`setStrokeStyle(Color)`、`setLineWidth`（忽略 ≤0 和非有限值）、`lineWidth()`、`setLineCap("butt"/"round"/"square")`、`beginPath`、`moveTo`、`lineTo`（空路径时等于 moveTo，忽略非有限坐标）、`stroke` |
| `effects.h` | `RectangularShadow`：`radius`、`blur`、`spread`、`offset`（`Point`）、`color`、`cached` |
| `controls.h` | `ProgressBar`（QtQuick.Templates 的属性） |
| `qt.h` | `qt::formatDateTime(DateTime, format)`（`Qt.locale().toString`，系统 locale，含本地化的星期和月份名）、`qt::locale().name()`、`qt::resolvedUrl(url, qmlFile)`、`qt::vector2d`、`qt::StandardPaths::standardLocations(...)`（返回 `file://` URL，无末尾斜杠；Cache 和 State 位置在 quickshell 下是 `~/.cache/quickshell`、`~/.local/state/quickshell`） |
| `js.h` | `js::dateNow()`（`Date.now()`，毫秒，double） |

还缺几项运行时能力：

- `Object::destroy()`（QML 的 `destroy()`）：要延迟执行，NotifTimer 会在自己的 `triggered` 里销毁自己。
- 指向对象的属性在对象销毁后自动置空并发出 `changed()`（QPointer 语义）。Notifications 依赖这一点。
- 能驱动动画组的 Behavior：`SequentialAnimation`/`ParallelAnimation` 里的 `PropertyAction {}` 在那一刻写入新值（StyledText 的文字动画）。
- `Animation::alwaysRunToEnd`。
- 单例的退出顺序：`instance()` 创建后从不销毁，退出时需要显式清理（例如结束 Process 子进程、写回文件）。

## Quickshell 兼容层（`src/compat`，命名空间 `ii::qs`）

| 头文件 | 内容 |
|---|---|
| `scope.h` | `Scope`、`Singleton` |
| `panel_window.h` | `PanelWindow`：`anchors.{left,right,top,bottom}`（bool）、`margins.*`、`exclusiveZone`、`exclusionMode`、`layershell.{layer,namespace_,keyboardFocus}`、`color`、`visible`、`implicitWidth/Height`（int）、`screen`、`mask`（`Region*`）、`contentItem()`。`Region`（`item` 等）。枚举 `WlrLayer`、`WlrKeyboardFocus`、`ExclusionMode` |
| `screen.h` | `ShellScreen`（`name` 等） |
| `quickshell.h` | 单例 `Quickshell`：`screens`、`shellPath()`、`execDetached(std::vector<std::string>)` |
| `hyprland.h` | 单例 `Hyprland`（`focusedMonitor`）、`HyprlandMonitor`（`name`）、`HyprlandWorkspace`、`GlobalShortcut`（`name`、`description`，`pressed` 属性和 `pressedSignal` 信号） |
| `ipc.h` | `IpcHandler`：`target`、`addFunction(name, fn(args) -> std::string)` |
| `process.h` | `Process`：`command`、`running`、`stdout_`（`DataStreamParser*`）、`exited(int, int)`。`StdioCollector`：`text`、`streamFinished`。`SplitParser`：`read(std::string)` |
| `file_view.h` | `FileView`：`path`（去掉 `file://`）、`text()`、`setText()`、`reload()`、`loaded` 属性和 `loadedSignal` 信号、`loadFailed(FileViewError::Enum)`、`fileChanged`、`adapterUpdated`、`blockWrites`、`watchChanges`、`writeAdapter()`。`JsonAdapter`、`JsonObject`：读写 JSON 需要按属性名反射，由生成器为其子类生成 `toJson`/`fromJson`/按路径设值（Config 的 `setNestedValue` 也依赖它） |
| `system_clock.h` | `SystemClock`：`date`、`precision`（`SystemClock::Enum`：`Hours`、`Minutes`、`Seconds`） |
| `pipewire.h` | 单例 `Pipewire`（`defaultAudioSink/Source`、`nodes`、`ready`）。`PwNode`（`audio`、`isSink`、`isStream`、`name`、`description`、`properties`）。`PwNodeAudio`（`volume`、`muted`、`volumes`）：`volume` 的变化信号就是 `volumes.changed()`，与 Quickshell 的 `volumesChanged` 一致，任一声道变化都触发。`PwObjectTracker`（`objects`） |
| `notifications.h` | `NotificationServer`：`trackedNotifications`、`notification(Notification*)`，以及能力属性。`Notification`：`id`、`tracked`（可写）、`expireTimeout`（毫秒）、`appName`、`appIcon`、`summary`、`body`、`image`、`urgency`、`actions`、`hints`（`js::Json`）、`dismiss()`。`NotificationAction`：`identifier`、`text`、`invoke()`。枚举 `NotificationUrgency` |
| `object_model.h` | `UntypedObjectModel`：`values`（`std::vector<Object*>`） |
| `color_quantizer.h` | `ColorQuantizer`：`source`、`depth`、`rescaleSize`、`colors` |
| `variants.h` | `Variants` 与委托运行时，见下 |

## 生成器要补的

- **委托**：`Variants`、`Repeater` 等以 Component 为默认属性的类型。要按模型项实例化，并设置 `required property modelData`/`index`。Brightness 目前在这里留了桩。
- **Loader 的 `source:` URL**：需要 URL 到生成类工厂的注册表。
- **JsonObject/JsonAdapter 的反射**：见上。
- **动画组**：Behavior 驱动动画组；Behavior 带 id 或自有属性。这两种目前都生成桩。
- **非单例外层文件的 id**：内联组件引用它时需要外层对象的指针（StyledText 的 `Anim`）。
- **var 类型的信号参数**：例如 `Notifications.notify(notification: var)`，现在是 `js::Json`。
- **多余的 `this` 捕获**：没用到 `this` 的 lambda 也捕获了它（clang 的 `-Wunused-lambda-capture`）。

## 照原样翻译的上游 bug

译文保留了 ii 现在的行为，每处在条目里都有注释。将来要修时从这里找：

- `GammaIndicator`：`gamma / 100 ?? 0.5` 的回退永远用不上。
- `BrightnessIndicator`：`?? 50` 是按 0–1 的刻度写的，找不到显示器时会显示 5000。
- `Brightness`：`monitor.animationEnabled` 不存在（应为 `animateChanges`），所以总是走计时器分支。
- `Notifications`：
  - `discardAllNotifications` 只关掉隔一个的通知。
  - `urgency` 的字符串是数字而不是名字。
  - `triggerListChange` 不起作用。
  - 被丢弃的 Notif 对象从不销毁。
- `Translation`：`isLoading` 从不为 true；翻译只在 `languageCode` 变化时加载。
- `Directories`：`.face` 头像路径少了一个斜杠（`/home/u.face`）。
- `SystemInfo`：输出里没有逗号时，解构会抛错，`windowingSystem` 保持不变。
- `WavyLine`：`x === 0` 的分支永远不会执行。
- `StyledText`：`onCompleted` 冻结了 `originalX/Y` 的绑定。
- var-types 中记录的其他问题见 `tools/qml2cpp/data/README.md`。
