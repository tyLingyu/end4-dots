# 阶段 3：运行时与 Quickshell 兼容层

OSD（`modules/ii/onScreenDisplay`）及其依赖共 24 个 QML 文件，由 [`tools/qml2cpp`](../tools/qml2cpp) 生成到 `src/ii/`。`ii-shell` 运行的就是这份生成代码，OSD 已上屏，可与 `qs -c ii` 同时运行。无头测试（`tests/generated/osd_test.cpp`）覆盖：

- 面板随 `GlobalStates.osdVolumeOpen` 加载、卸载；
- 指示器按 URL 加载；
- Config 写出的默认 `config.json` 与真实 Quickshell 写出的**逐字节相同**，并经文件监视重新加载后变为 ready。

API 命名约定见 [`tools/qml2cpp/FILL.md`](../tools/qml2cpp/FILL.md)。行为以 Qt 6.11 / Quickshell 7511545 的源码为准，移植处在代码中注明出处。

## 已完成（3a）

| 部分 | 内容 |
|---|---|
| 运行时 | Timer（QQmlTimer 语义）、Connections、Loader（deleteLater 卸载，`source` 走生成的组件表）、MouseArea（InputArea）、Canvas + Context2D（Cairo）、RectangularShadow（Qt 的几何与着色器）、Control/ProgressBar、FrameAnimation、`alwaysRunToEnd`、驱动动画组的 Behavior、`Object::deleteLater`、FdWatch、geometry/datetime、`qt.h`（formatDateTime、resolvedUrl、StandardPaths、locale）、`js::deref`（JS 的 TypeError） |
| 兼容层 | Process / SplitParser / StdioCollector（fork/exec + pidfd）、SystemClock、FileView（读写）、JsonAdapter/JsonObject（生成的反射，Qt 的 JSON 格式）、Variants（委托）、ObjectModel、Scope/Singleton、ShellScreen、Quickshell（路径、execDetached）、IpcHandler（注册） |
| 生成器 | Variants 委托、Behavior 动画组与内联动画组件、JsonObject 反射、模板字符串、字符串/数组方法、全局转换函数、组件表、单对象默认属性、可空指针访问 |

## 已完成（3b）：系统集成

各部分的测试在 `tests/compat/`，并都对真实会话（Hyprland 0.56.2、PipeWire）做过冒烟。

| 部分 | 内容 |
|---|---|
| `FileView.watchChanges` | inotify，掩码与同批事件合并照 Qt 的 inotify 引擎；文件与所在目录都监视（被替换、删除、新建的文件），`reload()` 重建监视，同 Quickshell |
| `compat/hyprland` | `.socket.sock` 请求（非阻塞，`MSG_NOSIGNAL`）、`.socket2.sock` 事件；monitors/workspaces/toplevels 模型、focusedMonitor/focusedWorkspace/activeToplevel、`dispatch`、`rawEvent`；Quickshell 处理的 16 种事件；对象间的反向指针靠 `Object::destroyed` 清空 |
| `GlobalShortcut`（`compat/global_shortcuts`） | hyprland-global-shortcuts-v1；同名 appid:name 引用计数；appid/name 变化时重新注册；`qs::setWaylandConnection()` 之前创建的快捷键在连接后注册 |
| `compat/pipewire` | `pw_loop` 挂在 FdWatch 上，不开线程；registry、节点、`default` 元数据（含 `preferredDefault*` 的写入）、PwObjectTracker 按需绑定；PwNodeAudio 的立方音量换算，有设备的节点经设备的 Route 参数改音量与静音（同 wpctl/pavucontrol），通知顺序 channels → volumes → muted，`volume` 在 `volumes.changed()` 之前已更新 |
| `compat/ipc` | `$XDG_RUNTIME_DIR/ii-shell/ipc.sock` 服务端；`ii-shell ipc call/show`（也接受 `msg`），输出与退出码同 `qs ipc`：连上实例就退出 0（ii 的快捷键靠 `ipc call TEST_ALIVE \|\| 备选` 判断存活）；第二个实例不会删掉在用的 socket |
| `ColorQuantizer` | Quickshell 的中位切分，缩放照 `Qt::KeepAspectRatio` + 平滑缩放；后台线程计算，结果回主线程，被取代的任务直接丢弃，不阻塞；参照值取自真实 Quickshell |
| 运行时 | `Object::destroyed`（QObject::destroyed，析构开头发出）；测试用的 `tests/pump.h` |

## 3b 剩下的

| 部分 | 要做的 |
|---|---|
| `compat/notifications` | org.freedesktop.Notifications 服务端（OSD 用不到，通知界面移植时做） |
| IPC 的信号与属性 | `qs ipc` 的 `listen`/`prop`（ii 没用到） |

## 已完成（3c）：上屏

| 部分 | 内容 |
|---|---|
| `PanelWindow`（`compat/panel_window`） | Noctalia 的 LayerSurface；anchors/margins/exclusiveZone/exclusionMode/layer/namespace/keyboardFocus/mask，下一帧前一并提交（同 Quickshell 的 polish）；可见、已 complete 且平台已设置时映射到 `screen` 的输出，否则取消映射（Quickshell 也不复用不可见的层窗口）；`contentItem` 渲染进窗口，指针事件走窗口自己的 InputDispatcher |
| `compat/platform` | Wayland 连接与渲染器（`setPlatform`，测试里不设，窗口不映射、快捷键挂起）；`Quickshell.screens` 随 Wayland 输出增删；各窗口共用的帧请求、动画 tick 与指针路由 |
| Canvas（`compat/canvas_textures`） | 画过的 Canvas 在下一帧前上传为纹理，销毁时释放；测试里像素留在 CPU |
| main.cpp | `Loader::setResolver(generatedComponent)`，创建 OnScreenDisplay，去掉阶段 1 的演示 |
| 运行时：`CreationScope` | 组件创建期间安装的绑定推迟到整棵树建好后求值，后装先求值，同 QQmlObjectCreator 的 finalize：初值能触发 `onFooChanged`（字面量不触发）。之前生成代码边建边求值，`Translation.onLanguageCodeChanged` 收不到初值，界面不翻译 |
| 生成器 | `Component { id: x }` 随树创建，早于任何绑定（`Brightness.monitors` 求值时要调 `monitorComp.createObject`）；NOTIFY 名与属性不同名时（`FileView.loaded` → `loadedOrAsyncChanged`）用属性的 `changed()` |
| Wayland | 不再绑定 wlr-foreign-toplevel、ext-foreign-toplevel-list、hyprland-toplevel-mapping：ii 走 Hyprland IPC，这些只带来窗口标题更新的唤醒；ext-workspace 只在有使用者时绑定（它没有 destroy 请求，绑定后丢掉的管理器会让后续服务端创建的对象 id 错位，连接以 “not a valid new object id” 断开） |

**验收**（Hyprland 0.56.2，eDP-1 1.25 倍缩放，发布构建）：

- 视觉：`tests/visual/compare.sh` 对比 qs 与 ii-shell 的音量 OSD，平均差 1.2/255，差值超过 32 的像素 605/36250，集中在文字边缘：Qt 的文字是子像素（LCD）抗锯齿，ii-shell 是灰度抗锯齿。
- 内存：PSS 启动后 51 MB，OSD 打开过一次后 69 MB（目标 ≤150 MB）；同时运行的完整 `qs -c ii` 为 495 MB。
- 空闲 CPU：30 秒 3 个 tick（约 0.1%），期间终端标题每 80 ms 变化一次（Hyprland 的 `windowtitlev2` 事件）。

## 生成器以后要补的

- **委托**：Repeater、Instantiator 的委托（Variants 已完成），以及 `index` 与模型角色。
- **内联组件**：引用非单例外层文件的 id，以及带声明的内联动画组件。
- **var 类型的信号参数**：例如 `Notifications.notify(notification: var)`，现在是 `js::Json`。
- **多余的 `this` 捕获**：没用到 `this` 的 lambda 也捕获了它（clang 的 `-Wunused-lambda-capture`）。

## 照原样翻译的上游 bug

译文保留了 ii 现在的行为，每处在条目里都有注释。将来要修时从这里找：

- `GammaIndicator`：`gamma / 100 ?? 0.5` 的回退永远用不上。
- `BrightnessIndicator`：`?? 50` 是按 0–1 的刻度写的，找不到显示器时会显示 5000。
- `Brightness`：`monitor.animationEnabled` 不存在（应为 `animateChanges`），所以总是走计时器分支。
- `Config`：首次启动（`illogical-impulse` 目录还不存在）时，`ready` 能否变为 true 取决于 `Directories` 里 `mkdir -p`（`execDetached`）是否抢在 FileView 建立监视之前完成；目录不存在则监视失败，写出的默认配置不会被重新加载。真实 Quickshell 通常赢（把 PATH 里的 `mkdir` 换成先 `sleep 0.5` 的脚本后，`READY=false`）；ii-shell 在 `osd_test` 里输了这场竞争，所以测试预先建好目录。之后的启动不受影响。
- `Notifications`：
  - `discardAllNotifications` 只关掉隔一个的通知。
  - `urgency` 的字符串是数字而不是名字。
  - `triggerListChange` 不起作用。
  - 被丢弃的 Notif 对象从不销毁。
- `Translation`：`isLoading` 从不为 true。
- `Directories`：`.face` 头像路径少了一个斜杠（`/home/u.face`）。
- `SystemInfo`：输出里没有逗号时，解构会抛错，`windowingSystem` 保持不变。
- `WavyLine`：`x === 0` 的分支永远不会执行。
- `StyledText`：`onCompleted` 冻结了 `originalX/Y` 的绑定。
- Quickshell 的 `SplitParser`：标记之后紧跟的字节不会被当作下一个标记的开头，所以 `"a\n\nb"` 读出 `"a"` 和 `"\nb"`（已原样移植）。
- var-types 中记录的其他问题见 `tools/qml2cpp/data/README.md`。
