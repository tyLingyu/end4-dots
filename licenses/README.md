# Licenses

This repository contains code from other repositories. Files containing such code should include a license notice, and a copy should be stored in this folder.

- `shell/`：大部分代码拷贝自 [Noctalia](https://github.com/noctalia-dev/noctalia)（MIT），许可证副本见 `shell/LICENSE.noctalia`，说明见 `shell/README.md`。
- `shell/src/runtime/js.h` 的 `js::detail::v4Sort`：移植自 Qt（`qv4arraydata_p.h` 的 `sortHelper`，Qt 6.11，GPL-3.0-only 授权选项），用来让 `Array.prototype.sort` 的结果与 quickshell 完全一致（V4 的排序不稳定）。
