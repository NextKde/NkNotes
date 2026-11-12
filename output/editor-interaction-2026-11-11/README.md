# VM 编辑器验证原始记录

运行环境为 `kos-vm-dev` 中的独立应用实例和合成笔记；本机未执行构建、测试或应用操作。面向使用者的说明与截图见 [编辑器交互复查](../../docs/editor-interaction-review.md)。

以 [final-native-summary.json](final-native-summary.json) 的逐项矩阵为准：独立检查的通过不能覆盖连续操作中的失败。它描述的是 VM 实际快照，不包含后来的本地修复。[pending-runtime-fixes.json](pending-runtime-fixes.json) 单独记录提交 `2133581` 中尚待运行的文件指纹及复验动作。

- `source-hashes.json`：VM 实际使用的文件 SHA-256，并非最终 Git 提交的完整指纹。
- `configure.log`、`build.log`、`editor-build.log`：该快照的 CMake 配置、Qt Release 编译及 npm bundle 构建输出。CMake 有 QML 插件链接目标诊断，最终应用已链接并在独立实例运行。
- `block-handle.log`：块手柄、选区、落点、取消、上下文目标与斜杠菜单的回归输出；退出码为 0。
- `editor-interaction-report.json`、`editor-interaction.log`：实际 WebEngine 编辑器中的六组集成检查，退出码为 0。拖动部分通过工具动作调用验证 DOM 落点计算，不能代替原生鼠标操作。
- `native.log`、`native-final.log`、`native-drag-report.json`：原生拖动、重复拖动与边缘滚动通过；连续移动后再次拖动的 Escape 取消失败。最终重跑中的“空剪贴板”断言在已经复制之后执行，前置条件不成立，不能据此认定产品回归。
- `native-cancel-report.json`、`native-cancel-explicit.log`：独立短文拖动中，明确指定窗口的 Escape 清除预览且松手不移动；越界松开也不改变内容。它不覆盖连续拖动后的 Escape 失败。`native-report.json` 是独立取消检查留下的同内容副本，不是总矩阵。
- `native-copy.log`、`native-copy-report.json`：点击实际复制行后，立即重开同一手柄菜单，粘贴块启用。
- `slash-cdp.log`、`slash-cdp-report.json`：编辑器索引到达第 16 项 `toc`。原始用例名称含有“native popup”，但断言仅覆盖编辑器状态；原生弹层仍停顶部，失败见 `slash-settled.png`。`native-slash.*` 另外保留原生输入路径的未通过记录。
- `advanced-editor-report.json`、`advanced-editor.log`：原始 Advanced 回归的断言失败。整个文档带合法尾部空段落，实际为 `/\n`；最终源码改为检查当前列表段落。
- `advanced-final-report.json`、`advanced-final.log`：使用 `AdvancedEditorRegression.safe-cdp.mjs` 临时副本顺序重跑，前九组通过，随后剪贴板图片导入等待超时；后续表格与最终元数据检查未到达。一次与原生驱动重叠的中间运行已作废，不用作通过证据。
- `app.log`：实际实例日志，未发现本轮弹层高度绑定循环或快捷键歧义，含 AT-SPI 能力警告。
- `ci-provisioning-failure.log`、`ci-provisioning-failure.json`：GitHub Actions 的完整日志和关联运行终态。Ubuntu 软件源 DNS 解析反复失败，作业满 60 分钟后超时，未进入 Flatpak 安装、应用编译或回归步骤。该结果不能作为代码构建通过或失败的依据。

`NativeEditorInteractionRegression.mjs`、`native-cancel-explicit.mjs`、`native-copy.mjs`、`slash-cdp.mjs` 和 Advanced 临时副本保留了当时的驱动，含实例窗口号、坐标和端点。它们是验证记录，复用前必须重新校准并确认可丢弃笔记；不要直接针对其他应用或个人笔记运行。

最终提交中的 `Main.qml`、`SourceEditor.qml` 增加了跨文档查询恢复；`EditorTools.qml` 增加了拖动时的键盘焦点与 Escape 处理，以及弹层条目更新后的选择／滚动同步。这些改动晚于 VM 快照，源文件传输被自动审批阻止，因此未运行复验。`NotesPopover.qml` 与快照之间仅有注释调整。

拖动与边缘滚动过程截图存在 Qt 与 WebEngine 帧不同步现象，保留用于诊断，不能据图宣称落点像素通过；实际顺序与滚动数值见日志。斜杠菜单的可见选择不同步在等待后仍存在，已作为失败记录，不能用 CDP 通过替代。
