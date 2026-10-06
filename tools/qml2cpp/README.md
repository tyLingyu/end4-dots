# qml2cpp

把 ii（`dots/.config/quickshell/ii`）的 QML 翻译成 ii-shell（`shell/`）的 C++。QML 结构（对象树、属性、绑定、anchors、布局、动画、信号处理）全部机械翻译。JavaScript 只机械翻译一个能保证正确的小子集，其余留桩，由 AI 按 [`FILL.md`](FILL.md) 填写，存在 `data/js/` 里。

## 用法

在本目录下运行（需要 Qt 6 的 `qmldom`，即 `/usr/lib/qt6/bin/qmldom`）：

```sh
python3 -m qml2cpp deps <qml...>           # 依赖闭包（组件、单例）
python3 -m qml2cpp gen  <qml...> [--deps]  # 生成到 shell/src/ii/，--deps 连同依赖一起
python3 -m qml2cpp todo <qml...> [--deps]  # 待填的 JS 条目
python3 -m qml2cpp fill <qml> <json>       # 写入译文
```

路径相对 `dots/.config/quickshell/ii`，例如：

```sh
python3 -m qml2cpp gen --deps modules/ii/onScreenDisplay/OnScreenDisplay.qml
```

`shell/src/ii/` 是生成物，不进版本库，可以随时从 QML 加上 `data/js/` 重新生成。某个模块移植完成后，它的生成代码才会成为源码。

## 结构

| 模块 | 作用 |
|---|---|
| `dom.py` | 用 Qt 自带的 `qmldom` 解析 QML（含每个表达式的 JS AST），整理成简单的模型。解析结果按内容哈希缓存在 `.cache/` |
| `jsast.py` | qmldom 的 `astRelocatableDump` 转成节点树 |
| `qmltypes.py` | 读取 `/usr/lib/qt6/qml` 下全部 `.qmltypes` 与 `qmldir`，得到内置类型的属性、信号、方法、枚举、原型。用 QML 实现的类型（如 Quickshell 的 FileView）从其 `.qml` 读取 |
| `registry.py` | 类型名解析：import、`qs.*` 模块、目录导入、内联组件 |
| `deps.py` | 依赖闭包 |
| `typemap.py` | QML/Qt 类型到 C++ 类型的映射，内置类型到 runtime 类的映射（Quickshell 类型在 `ii::qs`），组件的命名空间与类名 |
| `tsys.py` | 类型系统：每个类型的属性（C++ 类型、访问路径），分组属性与附加属性，alias，带自有属性的子对象（匿名类） |
| `jsexpr.py` | JS 子集的类型化翻译：字面量、属性链、运算符（保留 JS 语义）、条件、Math、枚举、信号发射、已知签名的函数调用、赋值与 if/return/变量 |
| `codegen.py` | 生成类。第一阶段建对象树（id 成为成员），第二阶段设属性、绑定、处理器、Behavior、Connections、Component 工厂。一个 QML 文件对应一对 `.h/.cpp`，内联组件在其中生成为 `File_Inline` 类 |
| `structs.py` | `data/var-types.json` 的结构体，生成 `ii/var_structs.h` |
| `jsstore.py` | `data/js/` 的读写 |
| `signature.py` | 解析译文里的 C++ 函数签名 |

机械翻译的规则是：**要么确定正确，要么不翻**。类型对不上、语义有出入（`&&` 返回操作数、`+` 的字符串拼接、`undefined`）时一律留桩，不猜。

## 生成的代码

以 `OnScreenDisplay.qml` 为例：

```cpp
class OnScreenDisplay : public qs::Scope {
public:
  OnScreenDisplay();
  Property<std::string> currentIndicator;
  Property<qs::ShellScreen*> focusedScreen;
  void triggerOsd();
  Timer* osdTimeout = nullptr;
  Loader* osdLoader = nullptr;
};

OnScreenDisplay::OnScreenDisplay() {
  osdTimeout = create<Timer>();
  osdLoader = create<Loader>();
  osdTimeout->interval.bind([=, this] { return ::ii::common::Config::instance().options->osd.get()->timeout.get(); }, "...");
  osdLoader->sourceComponent.set(Component<Object>([=, this](Object& owner) {
    auto* c8 = owner.create<qs::PanelWindow>();
    auto* columnLayout = c8->contentItem()->add<ColumnLayout>();
    columnLayout->anchors().horizontalCenter.bind([=, this] { return columnLayout->parent.get()->horizontalCenterLine(); }, "...");
    // ...
    return c8;
  }));
  osdTimeout->triggered.connectForever([=, this]() { /* onTriggered */ });
}
```

- 属性是 `Property<T>`，绑定是 `bind(lambda)`，依赖在求值时自动追踪（`shell/src/runtime/property.h`）。
- 单例是 `X::instance()`。`Component`、`sourceComponent` 和隐式组件都生成为工厂 lambda，工厂里的 id 是局部变量。
- 处理器连接在构造函数里，`Connections` 随 target 变化重连。

## 现状

OSD（`modules/ii/onScreenDisplay`）连同依赖共 24 个 QML 文件：1105 处翻译完成，其中 204 条 JS 由 AI 按 `FILL.md` 填写，其余为机械翻译；剩余 6 个桩。
生成代码还不能编译，因为它用到的 Quickshell 兼容层和部分运行时类型还不存在。阶段 3 要实现的内容，以及剩下 6 个桩各需要什么，都列在 [`shell/COMPAT.md`](../../shell/COMPAT.md)。

已知限制：

- `Variants`、`Repeater` 等以 Component 为默认属性的委托：需要带 `required property modelData` 的委托运行时，目前生成桩。
- `Loader { source: "x.qml" }`：URL 到生成类工厂的注册表还没有。
- Behavior 里是动画组，或 Behavior 带 id、带自有属性：生成桩。
- 内联组件引用外层文件的 id 时，只处理了外层是单例的情况（Qt 里 id 按创建时的上下文查找，ii 的内联组件都在本文件内创建）。
- 匿名子对象（带自有属性的子对象）上的函数和信号。
- 带剩余参数（`...args`）的函数，调用处不做机械翻译。

## 为什么用 qmldom

设计时计划用 tree-sitter-qmljs 解析。实现时改用了 Qt 自带的 `qmldom`：不增加依赖，解析结果与 quickshell 实际运行时完全一致（同一个 QML/JS 解析器），还直接给出每个表达式的 JS AST。
