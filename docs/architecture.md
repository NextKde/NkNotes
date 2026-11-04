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

编辑快捷键只在正文焦点且非组合输入时启用。撤销/重做通过 Shortcut.sequences 绑定 StandardKey，包含平台提供的全部标准组合。ApplicationWindow 不引入 KosApplicationWindow 的 Ctrl+B 侧栏行为。新建、保存、查找、链接、模式切换按后续阶段接入此命令表，M0 不放置无法工作的业务按钮。跨平台标准修饰键与 TextArea 内置快捷键优先级必须在 VM 验证。

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

主窗口建议 1120×740，侧栏 208、列表 276，正文区域占余宽；SourceEditor 使用 NotesTokens.readingWidth 将正文阅读栏上限设为 720，并以对称内边距居中，窄窗口保持两侧至少 28 的留白后随可用宽度收缩。680 最小宽下收起侧栏，保留列表与可编辑正文。尺寸属于应用初始令牌，集中在 NotesTokens；不是 Apple 官方数值。当前是静态单会话布局，无假笔记库、无持久化状态。可拖动分栏、窗口布局恢复与关联检查器由 M1/后续阶段补充。

主窗口使用 ApplicationWindow + KosWindowControls + KosWindowResizeHandles。动作声明为 maximize/minimize/close，真实位置由组件布局产生；未接入平台适配器，保持客户端回退。顶部无额外圆角悬浮卡片。正文与列表为普通材质；侧栏当前未接入有效下层采样，按共享规范使用 100% 不透明的贴边普通背景，无模糊采样，减少透明度和软件渲染时同样保持实心，前景不变。

顶部命令在当前原型中与滚动正文没有重叠，无需切换成覆盖式玻璃标题栏。轻档液态操作按钮与格式 LiquidPopover 采样 readingScene（正文与普通背景），两个 KosToolTip 显式绑定同源 readingBackdrop；消费者、采样器和 Overlay 弹层均不在源树内。弹层使用一层共享液态外壳和普通 LiquidPopoverItem，Esc/菜单键盘导航沿用共享控件。缺失纹理和减少透明度由共享组件回退。没有壁纸复制、桌面采样或 KWin 请求。

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

## M1a 文件存储服务（2026-11-04，实现待 VM 验证）

本段取代上文“尚无文件 IO”和“标识策略待数据层实现”的阶段描述；不表示规格或验收状态已更新。依据主控本轮授权确定以下技术默认值，并非用户逐条确认：新建 UTF-8 无 BOM / LF / 末尾 LF；已有文件缓存原始字节，无改动保存保持逐字节不变，编辑保留 BOM、统一 LF/CRLF 与末尾换行状态。非法 UTF-8、混合换行、裸 CR、NUL、U+2028/U+2029 明确只读。未知 front matter、语法、空白不经过 YAML 或 Markdown 序列化。

`src/storage/MarkdownFile` 是字节适配器；`NotesRepository` 是 QObject 异步门面，所有文件读取、递归扫描、JSON、写入、监听和恢复在专属 QThread 中执行。串行请求携带递增 requestId，返回 library/generation；控制器必须忽略旧库 generation 和旧编辑 revision 的响应。服务尚未注册 QML 类型，未修改 main、EditorSession 或 QML。析构等待已接收请求完成；正常退出控制器应先等待保存结果并展示错误，不能靠析构代替关闭流程。

新笔记 front matter 写 UUID `nextnotes_id`。已有无 ID 笔记只在 `.nextnotes/state.json` 建立 ID 映射，不自动改正文；复制出的重复 ID 在列表显式返回，保存拒绝覆盖。外置 ID 在应用重命名、回收恢复和重新打开后保留；无 ID 文件在应用外被重命名目前视为新导入，未做内容相似性身份推断。标题读取支持保守的顶层单行 front matter `title` 标量，然后首个有效 ATX / Setext 一级标题，最后文件名；不宣称完整 YAML/Markdown 解析（块标量、复杂转义等完整语义待 MarkdownEngine；标题扫描排除 front matter 与代码围栏）。重命名仅改路径，不改 title/H1 或跨笔记链接。

`createdUtc` 是首次纳入库元数据的 UTC 时间，`modifiedUtc` 是应用保存时间；列表 `fileModifiedUtc` 单独表示文件系统 UTC 时间，不混称正文创建时间。收藏及回收记录保存在元数据。`.nextnotes` 和 `assets` 在递归进入前被排除；路径拒绝绝对路径、空段、`.`、`..` 和符号链接组件，不清理附件或无关目录。库锁阻止本应用两实例并发写元数据。损坏 state.json 显式报错并保留原文件，不静默重建丢失收藏/身份。

### 后续控制器 API

统一调用 `request(operation, QVariantMap)`，收到 `finished(requestId, operation, result)`。通用结果为 `ok`、`error`、`detail`、`library`、`generation`；调用可携带 generation 防止库切换后的旧请求。错误码须由控制器翻译成“发生了什么、内容安全状态、下一步”，不能只展示内部 error/detail。

| operation | 参数与结果 |
| --- | --- |
| selectLibrary | path 为已存在的本地库绝对目录；返回 notes、folders、duplicateIds、unwatchedPaths。存在内存草稿时拒绝切库 |
| list | 同上列表；notes 包含 path/id/title/baseHash/favorite/createdUtc/fileModifiedUtc/readOnlyReason |
| createFolder | path 为库内相对路径，父目录必须存在；不覆盖同名对象 |
| read | path；返回 text、baseHash、readOnly、readOnlyReason，并缓存该版本原始字节 |
| create | path、text（正文）；创建新 ID front matter，不覆盖同名文件 |
| checkpoint | path、text、baseHash；持久化编辑恢复草稿，返回 recoveryDurable；失败仍留内存 |
| save | path、text、baseHash；必须先 read 同一基线。返回规范化后的 text/baseHash；控制器用返回基线更新会话，不能把较旧响应当作较新 revision 已保存 |
| saveAs | 新 path、text；精确保留传入文本；若保留已有 ID 会重复则拒绝。可显式传 newIdentity=true，仅替换单行 nextnotes_id 为新 UUID，其他正文不变；无 ID 则外置新 ID。原始冲突草稿不会被删除 |
| favorite | path、favorite 布尔值 |
| rename | path、target、baseHash；只修改文件路径，返回 oldPath/newPath，为 M4 链接事务预留事件信息 |
| trash | path、baseHash；保存源路径、原元数据、内容摘要后移动到应用回收区，返回 trashToken |
| restore | token，可选 target；默认恢复源路径，冲突不覆盖；恢复原 ID/收藏，校验回收文件摘要 |
| recovery | 返回 drafts（text/baseHash/diskHash/matchesBase）、memoryDrafts、trash；从不自动应用草稿 |
| discardDraft | path；显式删除该恢复草稿，供用户放弃或已确认另存后的收尾 |

保存先以 QSaveFile 持久化草稿，再检查基线摘要，再使用下述同目录 staging + renameat2 EXCHANGE 正文提交协议。QSaveFile 仅用于 JSON 元数据与草稿，均禁用直接写入回退。冲突返回磁盘摘要与文本，保留磁盘版本及本地恢复草稿。草稿写入也失败时返回 recoveryDurable=false，并保留内存；不得宣称此时断电可恢复。新建使用 renameat2 NOREPLACE，目标重名时不覆盖；失败可能保留 staging 与 intent，草稿保留且错误明确。重命名与回收先持久化恢复元数据，再移动，崩溃可能留下待处理记录但不会自动删除正文。恢复失败保留回收记录；成功移动但元数据收尾失败会返回错误与说明。

QFileSystemWatcher 监听目录及文件，150ms 单次定时器合并事件并重新扫描；首次选库建立摘要基线；内部扫描保留未通知变化并安排去抖扫描，自身成功修改只更新实际路径的已知摘要，移动只处理源与目标路径。`libraryChanged` 返回变化 path/hash/removed 及新列表，监听资源不足返回 unwatchedPaths；控制器应提示并提供 list 手动刷新。当前实现不主动重载已打开缓冲区：无脏编辑可由控制器 read，有脏编辑必须先 checkpoint 并展示冲突。不存在周期轮询。每次扫描为全库扫描，虽在后台但尚无 1 万篇性能结论。

### 边界、验证与后续工作

QtTest 新增字节保真、只读异常格式、冲突及重启恢复、稳定 ID 与重复 ID、收藏、重命名与回收恢复、同名/路径逃逸/符号链接、草稿写失败、权限失败、外部监听样例。测试仅编写，未在本机编译、运行或验证；VM 应以非 root 用户运行权限用例。磁盘满的真实文件系统注入、断电窗口和同步盘并发压力仍待 VM 专项验证。

哈希检查后任意外部编辑器仍可能写入；EXCHANGE 保留实际被替换 inode 并复核其摘要，发现竞态返回冲突且不自动回滚。这里不承诺跨进程原子 compare-and-swap 或同步协议。路径逐组件检查同样不等于抵御攻击者同时替换目录的 openat 沙箱。正常文件更改、预先存在的符号链接和重名均走显式拒绝路径；高对抗并发目录替换不在本轮可移植 Qt 文件 API 的保证内。

700ms 自动保存、切换/关闭时处理脏状态、QML 模型/错误文案、工作区布局与最近打开、搜索、附件导入、历史版本（不同于草稿）及 M4 跨笔记引用更新尚未实现。单行 front matter 解析器应由后续 MarkdownEngine 共享语法契约替换；不可将本层简化标题读取声称为完整 Markdown 语义分析。

### M1a 提交协议修订
正文提交在 Linux 使用 renameat2：新建使用 NOREPLACE；保存使用 EXCHANGE。
提交前同目录持久化 `.nextnotes-UUID.swap` 和 `.nextnotes/commits/UUID.intent`。
intent 第一行 JSON 包含目标相对路径、基线摘要和新摘要，第二行为 staging 绝对路径。
交换前 staging 是本地新版本，交换后是实际被替换 inode；两种状态均不自动删除。
恢复 API 返回 intents，人工根据目标及 staging 摘要判定阶段，不自动覆盖正文。
交换后旧 inode 摘要与基线不符时返回 conflict，保留草稿、旧 inode 和当前本地正文。
缺少 syscall 或文件系统不支持标志时明确 writeFailed，无普通 rename 降级。
保留文件目前不自动清理，尚无历史版本管理 UI；异常后的恢复操作仍需后续控制器呈现。

### M1a 第三轮回归与故障注入（仅供 VM 执行）

新增 `setextHeadings`（标题顺序、front matter、围栏、缩进代码）、`internalScanKeepsExternalChanges`（在工作线程内部扫描入口确定性写入外部内容，覆盖 list 和失败 save，之后等待真实去抖通知）、`corruptMetadataAndRecovery`（state、草稿与 intent 损坏保留原文件并显式失败）。权限测试原样保留，须非 root 运行。

`interruptedCommit` 启动当前测试程序的独立 `crashChild` 子进程。子进程在 intent 文件和目录 fsync 后或 EXCHANGE 返回后、目标目录 fsync 前调用 `_Exit`，父进程重开库，验证目标、staging、草稿与 intent 均可定位，并验证 recovery 不覆盖更新的外部正文。这是进程终止测试，不是物理断电、内核崩溃或文件系统丢失模拟。`crashChild` 单独普通运行会 skip，仅父测试设置环境调用。测试 seam 仅在工作线程空闲时设置，内部扫描 hook 不用于监听扫描。

VM 构建当前源快照后可运行 `./build/notes-repository-test setextHeadings internalScanKeepsExternalChanges corruptMetadataAndRecovery interruptedCommit permissionFailure`（按实际构建目录替换）。真实磁盘满槽 `realDiskFull` 通过 `NEXTNOTES_ENOSPC_DIR` 指定独立容量受限 tmpfs；不设置时明确 skip，设置后若文件系统容量大于 64MiB 则拒绝填充并失败。槽填满临时测试目录中的 filler，验证恢复写入失败、原文未变、输入仍在内存；释放 filler 后再保存验证恢复。它覆盖草稿写满路径，不能替代所有 staging/intent/fsync 故障的专项证据。

例如仅在 VM 同一命名空间中执行（测试二进制路径须改为当前快照构建路径）：

```sh
mkdir -p /home/amao/apps/nextnotes-enospc
unshare -Ur -m sh -c 'mount -t tmpfs -o size=2m tmpfs /home/amao/apps/nextnotes-enospc && NEXTNOTES_ENOSPC_DIR=/home/amao/apps/nextnotes-enospc /home/amao/apps/nextnotes/build/notes-repository-test realDiskFull'
```

以上用例本轮仅编写源码，未在本机编译或执行；必须记录 VM 源快照、构建和完整日志，包括 skip，不能据此宣称验收通过。剩余 fsync 返回失败、物理断电及 UI 恢复引导仍待对应验证/阶段实现。


## M1b 应用控制器与真实工作区（源码实现，待 VM 验证）

本节取代前文“未接入 QML / 未实现自动保存与搜索”的阶段描述。main 注册 EditorSession 与 NotesController 实例；NotesRepository 仍为独立工作线程。共享 Kos.Ui 与旁边组件仓库均未修改。

NotesController 单缓冲会话以 requestId、library、generation、document epoch、revision 关联异步响应。700ms 单次定时器触发保存；一次只允许一个 save 在途。回包只更新提交 revision 与 baseHash，不用返回 text 重写编辑器；在途期间的后续输入继续脏并再次保存。切换/关闭的 continuation 排空最新 revision 后执行。失败撤销 continuation，保留窗口与缓冲，显示恢复是否已持久化；用户需重试后再次切换或关闭。IME 组合期间不保存、不切换、不把编辑器改为只读；组合结束后继续排空。SourceEditor 仅在明确加载文档时替换文本，保留原生命令撤销语义。

读取失败保留旧缓冲。重读回包若 revision 已改变则拒绝应用。外部监听按 generation 过滤；干净笔记重读、脏/组合/删除状态 checkpoint 并提示冲突。保存仍由仓库摘要比较裁决。在重开库时先读取 recovery；有草稿的笔记以磁盘版本只读呈现，明确载入草稿（基线不同阻止覆盖）、复制到新文件或丢弃后读取磁盘。丢弃当前输入也先 read 成功再删除草稿。saveAs 显式新身份，原始草稿保持，恢复中心可显式清理；不会自动覆盖冲突正文。

仓库 search 新操作在 worker 执行扫描与 UTF-16 子串定位，支持 query、filter=all/favorite/folder:relative，返回 notes（summary/match）、allNotes、folders 与 duplicateIds。摘要和标题纯文本展示；正文命中位置可直接映射 Qt TextArea UTF-16 光标。列表排序按文件系统修改时间降序。全库搜索与 UI 没有文件 IO，同步的字符串筛选在 worker；当前仍为每次全扫描，规模性能未知。documentResult 新增 id 便于读完后稳定选择。重复 ID 显式警告、列表按 ID+路径区分，位置缓存对重复 ID 加路径区分；仓库的拒绝覆盖规则保持。

工作区通过 QSettings 保存窗口大小、导航/栏宽、字号/外观、每库每稳定 ID 的光标/滚动（无 ID 或重复 ID 使用路径回退）。文件夹侧栏使用 KosSidebar，缺少真实下层细节时选用规定的不透明普通回退；液态控件及浮层采样 readingScene，独立于消费组件和采样器。正文不被顶部工具栏覆盖，顶部保持普通连续底色。窄窗口单列切换列表/正文；三栏分界可拖动，库导航在窄窗口仍有文件夹面板。所有快捷键集中在 CommandRegistry，格式只在正文焦点生效，弹层和 IME 隔离。

恢复中心把 commits 的 journal 与 intent（包括绝对 staging 路径、基线和新摘要）显示为可选择复制文本，并提供文件管理器定位 .nextnotes 入口。明确解释交换前后 staging 身份不同，保留双方且不自动清理；这不是完整历史浏览/比较器。原路径无法恢复时可改用根目录新文件名，仍遵循 ID 与不覆盖检查。

新测试源码 NotesControllerTest 覆盖保存期间继续输入、切换和关闭排空、失败关闭阻止、组合输入、陈旧搜索/未知回包、冲突与读取失败保留、恢复草稿必须显式选择。NotesRepositoryTest 增加目录创建/列举、中文标题正文搜索、父目录缺失、跨目录移动失败原文不变及重开身份。全部仅源码编写与阅读，未本机编译/测试/启动 GUI/shader，也未宣称 VM 验收。还需 VM 运行完整测试与浅深色/IME/恢复/窄窗口人工验收；权限、磁盘满、物理断电、1MB 与 1 万篇性能仍按既有专项计划记录。

仍未实现 M2 即时排版、完善 Markdown 解析/链接命令、附件、标签、反链与关系图；没有相应占位按钮。无需自动扫描用户目录；启动只恢复此前明确选择的库，并按稳定 ID 查找最近笔记；恢复草稿先读取，未处理草稿使正文只读。库缺失或不可访问时回到选择入口。应用是 M1b 待验证源码，不代表产品规格全部完成或 M1 已验收。

本轮补充：切库先准备元数据和首次扫描，全部成功才提交库锁及代次；失败保留旧库读取、保存与关闭能力。启动恢复已授权最近库与稳定 ID 对应笔记，恢复草稿读取完成前不自动打开笔记。撤销/重做使用平台 StandardKey，格式菜单也提供入口（Linux 重做绑定以平台为准）。列表显示逻辑修改时间及当前保存/冲突状态。错误说明使用中文，不直接展示内部错误码和诊断 detail。上述改动与新增回归测试仅编写，须在冻结快照对应 VM 构建及运行，不代表 GUI 或性能验收。

### M1b 列表键盘路径修复（待 VM 运行验收）

笔记、侧栏文件夹和文件夹浮层采用同一应用层 KeyboardList：Tab 进入当前行，Up/Down/Home/End 仅移动键盘光标，Enter/Space 与点击、Accessible press 调用同一个激活函数。每行明确提供可聚焦、标题/状态和选中语义；笔记的已打开选择仍绑定控制器的稳定 ID 与路径，列表重排按 ID/路径恢复导航位置，不把键盘行号写入文档选择。笔记激活仍调用 openNote，沿用现有保存排空与异步切换处理。

恢复中心保留每条记录的真实操作按钮与只读正文，通过 Tab 可达；聚焦时滚动使目标可见，操作名称包含记录路径。格式/更多菜单关闭后延迟恢复编辑焦点，让新打开的弹层或正文查找保留焦点；Ctrl+Shift+F 在列表显示后进入全局搜索，不改正文。只有普通笔记行使用 AppTheme.focusRing，没有增加液态控件蓝环。空状态标签不再在标题前拼接分隔符。

本轮仅源码编辑，未在本机编译、测试或启动 GUI。VM 需复测：搜索 Tab 进入笔记行、方向键滚动、Enter/Space/辅助技术 press 打开、AT-SPI FOCUSABLE/SELECTED/name、排序/搜索后稳定选择、文件夹键盘过滤、恢复中心 Tab 滚动，以及菜单 Escape/命令后返回正文与 Ctrl+Shift+F 后正文不变。

### M1 恢复与身份补充（实现待 VM 验收）

恢复中心以文件名、记录时间及保存前/本次待保存版本展示预览；无法确认的版本禁用另存。`recoveryVersion {token, version: before|pending}` 重新读取库内提交记录，校验目标/同目录 staging 路径与 SHA-256，返回 `text`、`bytes`、`hash`；`recoverCommit` 使用同一校验再生成新身份恢复副本，不覆盖原文件，保留记录和草稿。外部改变后不匹配任何记录摘要的版本拒绝恢复。恢复副本只调整身份字段，不整文重排。

允许源码编辑唯一的 nextnotes_id：保存成功后同步当前选择、最近笔记及位置键；重复 ID 仍拒绝保存。另存成功立即记录新文档，直接关闭重开可恢复副本；移动后通过读取新路径激活文档。外观使用 LiquidSelect、SettingsSwitch 与普通材质 LiquidSlider。

新增控制器身份/另存重开测试，并将真实子进程中断提交测试扩展为经恢复 API 读取两版本、独立字节核对及另存、损坏记录拒绝。仅源码实现，未在本机运行构建、测试或 GUI；须在虚拟机验收，不替代尚缺的 IME、键盘、主题及规模证据。
