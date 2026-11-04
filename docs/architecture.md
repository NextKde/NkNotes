# M0 工程、编辑器与文件约定

日期：2026-11-04。状态：实现中、待虚拟机验证。本文是实现决策记录，不更改产品规格，也不代表 M0 退出条件已经满足。

## 工程边界

工作目录为 NkNotes，工程暂用 NextNotes，程序名 nextnotes，尚未冻结发布用应用标识。独立 Qt 6.8+ C++17 / Qt Quick 宿主；不依赖 Quickshell、KWin 或 NextKde 服务。默认共享依赖为 Git 子模块 `third_party/nextkdeAppDemo`，上游 https://github.com/NextKde/nextkdeAppDemo.git，精确提交 `608847c9231121b888f11c4e5f84850ac95deda4`。不使用旁边组件工作区的未提交 API。

CMake 链接 Kos::Ui 与 Kos::UiPlugin，QML 使用 Kos.Ui。开发覆盖仅通过 KOS_UI_SOURCE_DIR 显式提供；正式构建不得使用任意未固定的本地覆盖。Qt 开发依赖：Core、Gui、Qml、Quick、QuickControls2、QuickDialogs2、Svg。运行环境还需 QtQuick.Layouts、QtQuick.Window、QtQuick.Effects、QtQuick.Dialogs 和 Qt5Compat.GraphicalEffects QML 模块及 SVG 图像插件。保留上游 LICENSE / NOTICE.md 和预编译 QSB，不重新烘焙 Shader。

本机仅阅读、编辑与 Git 操作。工程未在本机编译或运行；测试文件尚未执行。VM 验证必须由主控先固定源快照，再按同一快照传输和运行，证据保存在工作流状态目录，不混入产品文档。

## 原生与嵌入方案比较

| 维度 | 原生 Qt 文档原型 | 嵌入式 Markdown 内核候选 |
| --- | --- | --- |
| 中文输入 | QML TextArea 原生组合输入，组合期间拒绝格式命令；需实测候选与撤销 | 浏览器输入法、焦点桥接需另做实测 |
| 源码保真 | 直接编辑纯文本，不用 HTML/Markdown 全文往返；适合 M1 | 仅接受以源文本及位置映射为权威的内核；DOM 序列化不可作为正文 |
| 即时排版 | 需要增量解析与隐藏语法映射；普通 TextArea 不具备此能力 | 可能已有范围装饰、语法树与映射机制，具体能力未验证 |
| 选区/撤销 | QTextCursor + 同一 QTextDocument 原生撤销栈；一次格式操作一个 edit block | JS 与 QML 必须共用事务及版本化消息，避免双撤销栈 |
| 长文档 | 当前全文 source() 拷贝为 O(n)，还没有性能基线 | 需比较虚拟化、WebEngine 进程启动与内存成本 |
| 工具栏桥接 | 直接调用 EditorSession，UTF-16 位置与 Qt 一致 | IPC/JS 消息、异步焦点和选区恢复需额外协议 |
| 外观 | 直接使用共享 Kos.Ui | 必须隐藏网页工具栏并将主题、字号、减少动态注入内容 |
| 安全与依赖 | 不渲染 HTML，不发起远程资源请求 | 禁止脚本化用户内容和远程资源，审核网页运行隔离 |
| 许可/部署 | Qt 许可按所用发行版审核，Kos.Ui 保留 GPL-3.0 | 候选编辑库尚未选定；许可、QtWebEngine 和打包成本待比较 |

当前决定仅为：采用原生源码编辑原型支持后续 M1，不锁定最终即时编辑内核，不引入未经批准的第三方编辑库。嵌入方案尚未做可执行原型，不能据此宣称原生已经胜出。M2 前需对同一中文、长文档、模式切换与撤销样例做对照。

## 会话契约

EditorSession 包装 QQuickTextDocument 的 QTextDocument；同一纯文本 Markdown 缓冲区是编辑权威，不维护第二份可编辑 HTML。QML 输入和 QTextCursor 命令共享文档的原生撤销栈。source() 使用 toRawText 并将文档段落分隔符转成 LF，保留 NBSP；它是内存文本接口，不是字节保真文件解码器。

选区以 UTF-16 anchor / position 表达，保留方向。命令修改前复制选区，避免文档变更导致 QML 光标通知重入时覆盖命令目标。鼠标点击格式入口不主动获取编辑焦点，persistentSelection 保留选区；键盘 Tab 仍可进入工具栏。格式菜单关闭并执行后回到正文。输入法组合期间 execute / undo / redo 均拒绝执行，不进行解析回写。

当前支持相邻标记的插入/移除；这属于源码转换，不是语法感知的 Markdown 状态判断。混合选区、嵌套标记、代码围栏内上下文、链接编辑、跨段语义以及格式状态反映留到 MarkdownEngine。UI 因此使用格式命令菜单，不显示虚假的“当前已加粗”勾选态。即时模式不显示可操作的假入口。

## 集中命令表

命令注册位置：qml/CommandRegistry.qml。格式菜单调用相同 run → EditorSession.execute；撤销重做调用同一会话。

| 命令 ID | 快捷键 | 当前范围 |
| --- | --- | --- |
| bold | Ctrl+B | 原型相邻 ** 标记 |
| italic | Ctrl+I | 原型相邻 * 标记 |
| strike | Ctrl+Shift+X | 原型相邻 ~~ 标记 |
| code | Ctrl+E | 原型相邻反引号标记 |
| undo / redo | Qt StandardKey.Undo / Redo | 同一文档撤销栈 |
| sidebar | Ctrl+Shift+B | 侧栏偏好；窄窗口仍自动收起 |

编辑快捷键只在正文焦点且非组合输入时启用。ApplicationWindow 不引入 KosApplicationWindow 的 Ctrl+B 侧栏行为。新建、保存、查找、链接、模式切换按后续阶段接入此命令表，M0 不放置无法工作的业务按钮。跨平台标准修饰键与 TextArea 内置快捷键优先级必须在 VM 验证。

## 最小文件约定（M1 实现目标，尚无文件 IO）

- 新笔记为 UTF-8 无 BOM，LF 换行，末尾一个 LF；不把已有文件转换为该默认风格。
- 读取时必须保存原始字节、BOM、换行序列和末尾换行信息，严格解码 UTF-8；无改动保存必须逐字节保持原样。非法编码禁止静默替换后写回。
- 统一 CRLF 或 LF 文件编辑后保留原换行风格和末尾换行状态。混合换行、字面 U+2029/U+2028 需要 source-to-document 映射或保守只读导入；不能直接使用本原型 source() 覆盖这些文件。M1 文件适配器未实现前，不得承诺打开保存保真。
- 未知 front matter、注释、扩展与空白不重排。格式修改只作用于选定字符范围，不通过 Markdown→HTML→Markdown 保存。
- 基础格式目标沿用规格第五节；知识扩展目标沿用第六节。M0 只落地四个源码格式命令，不将其他目标写成已支持。
- 新建笔记未来生成 UUID，拟写入 nextnotes_id；现有笔记导入默认不自动改写，稳定 ID 在库元数据中映射。front matter ID 冲突必须显式报告，不任意合并。该持久化策略在 M1 数据层实现前需主控确认。
- 标题优先 front matter title，再首个一级标题，再文件名；重命名默认仅修改文件名，其他字段必须显式选择，避免隐式改正文。M0 样本文档无文件/ID，不用于证明这些业务流程。
- assets 与 .nextnotes 保留作附件和可重建元数据位置。索引不得作为正文唯一来源。

## 三栏与关键浮层设计

主窗口建议 1120×740，侧栏 208、列表 276，正文占余宽；680 最小宽下收起侧栏，保留列表与可编辑正文。尺寸属于应用初始令牌，集中在 NotesTokens；不是 Apple 官方数值。当前是静态单会话布局，无假笔记库、无持久化状态。可拖动分栏、窗口布局恢复与关联检查器由 M1/后续阶段补充。

主窗口使用 ApplicationWindow + KosWindowControls + KosWindowResizeHandles。动作声明为 maximize/minimize/close，真实位置由组件布局产生；未接入平台适配器，保持客户端回退。顶部无额外圆角悬浮卡片。正文与列表为普通材质；侧栏贴边普通背景单独 0.95 不透明度，前景不变。

顶部命令在当前原型中与滚动正文没有重叠，无需切换成覆盖式玻璃标题栏。轻档液态操作按钮与格式 LiquidPopover 采样 readingScene（正文与普通背景），消费者、采样器和 Overlay 弹层均不在源树内。弹层使用一层共享液态外壳和普通 LiquidPopoverItem，Esc/菜单键盘导航沿用共享控件。缺失纹理和减少透明度由共享组件回退。没有壁纸复制、桌面采样或 KWin 请求。

后续选区工具条锚定 selectionRect，若与输入法候选或选区相交则调整到选区上/下方；无法获得候选几何时组合输入阶段隐藏。链接/重命名使用 LiquidContentPopover 的独立草稿与提交；破坏性确认用 LiquidDialog。当前仅格式选择器已实现，其余是设计计划。

应用颜色只用 AppTheme；黄色识别色暂不启用，主确认动作以后仍采用共享固定蓝色。主题/减少透明度/减少动态使用共享语义；用户偏好持久化及设置入口尚未实现。

## 组件差距与后续模块

| 项目 | 固定提交事实 | 处置 |
| --- | --- | --- |
| KosApplicationWindow | 内置 Ctrl+B 侧栏行为 | 本工程自定义宿主，复用共享窗口控件 |
| KosAppWindow | 固定提交不存在，本地旁仓新增未提交 | 不依赖、不复制 |
| KosSidebar | 负责布局/动画，不自动背景模糊 | 普通贴边背景，当前无正文在侧栏下滚动；覆盖场景后续组合真实采样 |
| KosTextArea | 普通主题文本域 | 不称作 Markdown 内核；原型直接 TextArea + EditorSession |
| VectorIcon | 有 bold、undo、folder、panelLeft、editRounded、plus 等；缺少 link、table、italic、redo、note 等专用名称 | 当前采用明确文字按钮，不用字体符号；后续先在共享库补齐向量图标 |
| 文档核心 | EditorSession 已有选区/事务入口 | MarkdownEngine 增量解析独立模块，带 revision 的后台结果应用 |
| 存储/模型 | 尚未实现 | NotesRepository、NotesModel、FolderModel 与 WorkspaceState 在 M1 接入 |
| 索引 | 尚未实现 | SearchIndex / LinkIndex 可重建，异步结果携带文档 ID 与 revision |

## VM 验证计划与未知项

kos-vm-dev（已知 Qt 6.10.2，CPU/RAM/GPU/输入法待记录）。先保存源码/配置/子模块锁/规格快照，再运行相同快照版本。测试包含未知元数据局部修改、一次格式撤销、反向选区、NBSP 和组合输入保护；GUI 另测中文长句候选、格式菜单不丢选区、Tab/Escape、内置撤销与快捷键竞争、最小宽度、深浅色和减少效果。不能用单元测试替代 GUI 输入法证据。

性能场景采用规格提案：1MB 长文档、1万篇短笔记。M0 仅对长文档编辑建立基线，笔记库规模等 M1 有模型/索引后再测。记录 CPU/RAM/GPU、Qt 后端、分辨率、输入法、文档内容与行数；测打开耗时、输入到画面更新 p50/p95、一次格式/撤销延迟、进程 RSS 与 GPU 占用。暂定观察目标为输入 p95 < 50ms、格式/撤销 < 100ms（非验收已通过），根据实测与目标 VM 配置再冻结预算。没有运行数据，不宣称满足目标。

本轮未解决：运行/视觉证据、嵌入对照实验、即时映射方案、原始字节文件适配器、真实保存/恢复、混合 Markdown 状态、持久化偏好。M0 当前不能标记完成；下一步先验证原生关键编辑路径，再修订内核决策与推进 M1。
