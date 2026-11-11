# VM 编辑器验证原始记录

运行环境为 `kos-vm-dev` 中的独立应用实例和合成笔记；本机未执行构建、测试或应用操作。面向使用者的说明与截图见 [编辑器交互复查](../../docs/editor-interaction-review.md)。

- `source-hashes.json`：VM 实际使用的文件 SHA-256。它记录的是 VM 快照，并非宣称与最终 Git 提交逐文件相同。
- `block-handle.log`：块手柄、选区、落点、取消、上下文目标与斜杠菜单的回归输出；VM 执行退出码为 0。
- `editor-interaction-report.json`、`editor-interaction.log`：实际 WebEngine 编辑器中的六组集成检查，VM 执行退出码为 0。拖动部分通过工具动作调用验证 DOM 落点计算，不能代替 QML 手柄的原生鼠标操作。
- `advanced-editor-report.json`、`advanced-editor.log`：较早一次 Advanced 回归的失败原始记录。断言将整个文档的纯文本与 `/` 比较，但编辑器保留了合法的尾部空段落，实际为 `/\n`；最终源码改为检查当前列表段落。此处保留原始失败，不将它计作完整套件通过。

最终提交中的 `Main.qml`、`SourceEditor.qml` 增加了跨文档查询恢复，晚于该 VM 快照；源文件传输被自动审批阻止，因此这些记录不覆盖该修复的运行结果。`NotesPopover.qml` 与快照之间仅有注释调整。`AdvancedEditorRegression.mjs` 的断言修正也晚于 `source-hashes.json`；其重跑情况以图文说明为准。
