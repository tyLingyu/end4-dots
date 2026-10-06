# 填写 JS 翻译（data/js）

qml2cpp 只机械翻译能确定无误的 JS 子集，其余的在生成代码里留 `II_TODO_JS("<key>")`（编译错误），并把条目记在 `data/js/<qml 路径>.json`。本文是填写这些条目的约定，AI 和人都按这个来。

## 流程

```sh
cd tools/qml2cpp
python3 -m qml2cpp todo <qml...>          # 列出待填条目（JSON，按文件分组）
python3 -m qml2cpp fill <qml> <译文.json> # 写入：{context: {"cpp": ..., "signature": ...}}
python3 -m qml2cpp gen --deps <qml...>    # 重新生成，看桩是否消失
```

`<qml>` 是相对 `dots/.config/quickshell/ii` 的路径（如 `services/Audio.qml`）。生成结果在 `shell/src/ii/<同路径>.h/.cpp`，在 `.cpp` 里搜 key 就能看到桩所在的位置和周围的代码。

已填的条目（`cpp` 非空）重新生成时直接使用；之后机械翻译能处理了的条目不再使用，但会保留。

## 条目

| 字段 | 含义 |
|---|---|
| `code` | 原 JS |
| `context` | 位置：`对象路径.属性`，对象路径由 id 或 `类型.序号` 组成 |
| `kind` | 什么构造，括号里是机械翻译放弃的原因 |
| `form` | 要写成什么（见下） |
| `expected` | 表达式的 C++ 类型，或处理函数的参数 |

### form

- **`expression`**：一个类型为 `expected` 的 C++ 表达式，生成为 `x.bind([=, this] { return <cpp>; })`。读到的属性（`.get()`）自动成为依赖。需要多条语句时写成带 `return` 的函数体，例如 JS 的块绑定 `{ let x = ...; return y }`。
- **`statements`**：C++ 语句，原样放在桩所在的位置（组件构造函数里，或 Component 工厂 lambda 里，工厂里的局部变量可见）。常见的是整段写出一个机械翻译做不了的绑定、连接或对象。
- **`body`**：信号处理函数的函数体。`expected` 写着 lambda 的参数；生成器把它包进 `signal.connectForever([=, this](参数) { ... })`。
- **`function`**：组件的成员函数。填 `signature`（C++ 声明，如 `"double clamp01(double x)"`，可带默认参数）和 `cpp`（函数体）。填了签名后，别处对它的调用就能机械翻译，所以**优先填函数**。

## 写 C++ 时能用什么

生成代码在 `namespace ii::<目录>` 里，组件根对象就是 `this`。

- **属性**：QML 属性 `foo` 是成员 `Property<T> foo`，读用 `x->foo.get()`，写用 `x->foo.set(v)`（写入会断开绑定，与 QML 赋值一致）。
- **分组和附加属性**：`anchors.*` 是 `anchors().*`，`font.*` 是 `font.*`，`border.*` 是 `border.*`，`Layout.*` 是 `layout().*`。PanelWindow 的 `anchors.top` 等是布尔值，`margins.*` 是边距，`WlrLayershell.layer`/`namespace` 对应 `layershell.layer`/`layershell.nameSpace`。完整的表见 `qml2cpp/tsys.py` 的 `GROUPS`/`ATTACHED`。
- **信号**：信号是成员 `Signal<Args...> name`。发射用 `x->name.emit(args...)`，连接用 `x->name.connectForever(lambda)`（与对象同寿命）。属性变化信号 `onFooChanged` 是 `x->foo.changed()`。
- **函数**：方法与 JS 函数同名，是成员函数。
- **改名规则**（`qml2cpp/typemap.py` 的 `cpp_name`、`tsys.py` 的 `signal_member`）：
  - C++ 关键字和 `stdout`/`stderr`/`stdin`/`errno` 等宏名加 `_` 后缀，例如 `proc->stdout_`、`layershell.namespace_`。
  - 属性和信号同名时，属性保留原名，信号加 `Signal` 后缀。例如 MouseArea 的 `pressed` 属性写成 `pressed.get()`，`onPressed` 写成 `pressedSignal`；FileView 的 `loaded` 信号写成 `loadedSignal`。
- **id**：在构造函数里是成员（`osdTimeout`），在工厂里是局部变量。名字看生成的 `.h`/`.cpp`。
- **单例**：本项目的单例写成 `::ii::services::Audio::instance()`（类名见生成的头文件）。Quickshell 的单例写成 `qs::Quickshell::instance()`、`qs::Hyprland::instance()`、`qs::Pipewire::instance()`。
- **Quickshell 类型**：在 `ii::qs` 里，写成 `qs::Process`、`qs::FileView` 等，API 与 `.qmltypes` 逐一对应（`/usr/lib/qt6/qml/Quickshell/**/*.qmltypes`），按上面的属性、信号、方法规则命名。枚举 `X.Value` 写成 `qs::X::Value`。类型映射见 `qml2cpp/typemap.py`。
- **类型**：`string` 是 `std::string`，`real` 是 `double`，`color` 是 `Color`，`list<T>` 是 `std::vector<T>`。`property var` 的类型以生成的 `.h` 为准，结构体在 `shell/src/ii/var_structs.h`。没有固定结构的值用 `js::Json`（`nlohmann::ordered_json`）。
- **Component**：`comp.createObject(parent)` 写成 `comp.createObject(*parent)`；带初始属性的 `createObject(p, { a: x })` 写成 `comp.createObject(*p, [&](T& o) { o.a.set(x); })`。
- **颜色**：`Qt.rgba`、`Qt.hsla`、`c.hslHue` 等对应 `qt::rgba`、`qt::hsla`、`qt::hslHue(c)`（`shell/src/runtime/color.h`）。颜色分量是 `c.r`/`c.g`/`c.b`/`c.a`。

## JS 语义：照 Qt 的 V4 原样翻译

ii 跑在 Qt 的 JS 引擎上，翻译要得到**与 ii 现在一模一样**的结果。

- 字符串、数字、正则、数组、JSON、console 一律用 `ii::js`（`shell/src/runtime/js.h`），不要手写等价物。例如 `s.split(",")` 写成 `js::split(s, ",")`，`x.toFixed(1)` 写成 `js::toFixed(x, 1)`，`String(n)` 和数字拼进字符串时写成 `js::toString(n)`，`/re/g` 写成 `js::Regex("re", "g")`，`JSON.parse`/`JSON.stringify` 写成 `js::parse`/`js::stringify`，`console.log(a, b)` 写成 `js::log(a, b)`。这些函数的边界情况已对着 V4 测过：字符串下标按 UTF-16，`toFixed` 的进位，`sort` 不稳定。
- `Math.round` 写成 `js::round`。JS 的算术都是 double，整数属性赋值时截断。
- 真值判断：`0`、`NaN`、`""`、`null`、`undefined` 为假。`==` 会做类型转换，`===` 不会。
- 读不存在的成员得到 `undefined`。照它的后果翻译，**不要修正**上游的 bug。例如 `monitor.animationEnabled` 实际不存在，条件永远为假，就直接翻成走 else 分支，并留一行注释 `// ii: animationEnabled 不存在（属性叫 animateChanges），恒为 undefined`。
- `a?.b` 和 `a ?? b` 写成显式的空检查。`undefined`/`null` 按目标类型对应到 `nullptr`、`std::nullopt` 或默认值，并在注释里说明。
- JS 对象字面量对应 var-types 的结构体（如 `OsdIndicator{"volume", "indicators/VolumeIndicator.qml"}`）或 `js::Json`。
- 回调和箭头函数写成 C++ lambda；数组方法用 `js::map`/`js::filter`/`js::find`/`js::sorted`。

## 不要做的事

- 不改生成代码，不改其他文件的条目，不改 qml2cpp。
- 不发明约定之外的 API。如果确实需要一个 runtime 还没有的东西（`Qt.locale().toString`、`Qt.resolvedUrl`、`Quickshell.execDetached` 等），写成 `ii::qt` 或对应 `qs::` 类型上的同名函数，并在汇报里列出来，留给阶段 3 实现。
- 调用其他组件的 JS 函数时，先看它在 data/js 里有没有 `signature`。没有的话按 JS 推断一个签名，并在汇报里写明假设。

## 汇报

填完后给出：

- 每个文件填了几条、跳过了哪些，以及跳过的原因。
- 用到但需要阶段 3 提供的 API。
- 对其他组件函数签名的假设。
- 发现的上游 bug（按原样翻译了的）。
