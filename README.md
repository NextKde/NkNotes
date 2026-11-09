# NextNotes / NkNotes

面向 NextKde 的原生 Qt Quick 笔记应用：SQLite 管理内部 Markdown 笔记、三栏导航、所见即所得编辑、700ms 自动保存、中文搜索、收藏、虚拟文件夹和最近删除。首次打开创建空的内部笔记空间，不扫描个人目录、不创建演示数据。正文支持附件导入与可恢复清理；支持安全移动／重命名、非空文件夹整理和应用内 Markdown 链接导航；现支持笔记历史、搜索索引、标签／属性、双链补全与反链、智能文件夹、模板和每日笔记。

当前存储与导入方向以 [SQLite 管理说明](docs/managed-notes.md) 为准；下文旧文件仓库说明保留为历史实现记录。

产品依据：[产品规格](docs/PRODUCT_SPEC.zh-CN.md)。正文功能、存储边界和 VM 验证记录见[正文编辑说明](docs/body-editor.md)，笔记整理与批量接口见[整理操作说明](docs/organizer.md)。实现边界、编辑器比较、命令表、最小文件约定和组件差距：[架构记录](docs/architecture.md)。本次编辑器验证范围见下文，不代表全部产品验收。

“设置 → 高级”提供 Markdown 源码和恢复草稿入口；源码与正文共用撤销历史。笔记／文件夹恢复、永久删除和清空在侧栏“最近删除”完成。操作边界见上述正文与整理说明。

## 知识组织与记录（2026-11-05）

笔记菜单中的“笔记信息”提供标签、关联与历史；模板、智能集合和批量标签管理位于“设置 → 高级”。

- 历史：保存前记录旧内容，按稳定身份保留最多 50 个不同内容版本；支持与当前源码并排对照、恢复及撤销恢复。
- 搜索：后台内存子串索引，支持中文；可组合 `tag:工作`、`tag:"project alpha"`、`folder:项目`、`is:favorite`、`prop:status=done` 和正文查询。智能文件夹保存当前条件与范围。
- 双链：缺失目标提供“创建并打开”，按来源笔记解析相对路径，已存在目标不覆盖。正文输入 `[[` 弹出候选；支持键盘选择、别名、标题片段及 Ctrl+点击应用内导航。关联面板提供目标／标题选择和反链上下文。
- 标签管理：列表多选后批量添加／移除标签；全库重命名先预览影响篇数，合并重复标签并跳过发生外部修改的笔记。
- 属性：标签写入 front matter 的 `tags`；简单属性写回 Markdown，保留其他字段。复杂 YAML 可用源码模式编辑。
- 模板：保存当前笔记或自定义 Markdown，支持 `{{date}}`、`{{time}}`、`{{title}}`。每日笔记使用“每日笔记”模板，路径为 `日记/YYYY-MM-DD.md`；当天已有文件则直接打开。

万篇笔记规模验证及优化已完成：反链查询约 91 毫秒，万行重排约 35 毫秒，千文件外部变更约 2.1 秒后进入索引。约 2 MB、1.6 万段长文的正文打开已从 13.41 秒降至约 8～10 秒；源码采用可见区域渲染，打开从约 42 秒降至约 1 秒。正文编辑的 Web 阻塞从约 2.38 秒降至约 0.4～0.62 秒，源码编辑从约 35 秒降至约 0.1 秒。正文超长文仍有可感知停顿，具体测量条件与边界见实测记录。

实现范围、持久化位置与验证见[整理操作说明](docs/organizer.md)。管理空间现支持独立完整备份与恢复，见下文说明；自动冲突合并和附件健康检查尚未加入。

## 当前所见即所得编辑器（2026-11-05）

主编辑器使用 [Milkdown](https://github.com/Milkdown/milkdown) 7.23.0（MIT），基于 ProseMirror / remark，启用 CommonMark、GFM、剪贴板、历史、光标、缩进和尾部段落插件。Qt Quick 保留现有 Kos.Ui 工具栏、导航、弹层及应用状态；网页只负责正文编辑。此前的 QTextDocument 实现保留供旧契约测试和独立原型使用，主界面不再依赖逐项自制输入规则。

- 段落、标题、粗体、斜体、删除线、行内代码、引用、代码块、分隔线、链接和本地库内图片。
- 行首 `# ` 至 `###### `、`- `、`1. `、`- [ ] `、`> ` 等输入规则；正文 Enter 分段、Shift+Enter 换行，列表 Enter 续项、空项 Enter 退出，Tab / Shift+Tab 缩进。
- GFM 表格：输入完整表头与分隔行后按 Enter、粘贴 Markdown 表格，或输入 `|3x3| ` 建立三列三行表格。顶部“表格与行列操作”提供增删行列、列对齐、删表和退出；表格内 Tab / Shift+Tab 导航，Ctrl+Enter 退出。
- Markdown 及富文本粘贴、统一撤销/重做；格式按钮和快捷键调用同一编辑内核命令。

正文工具已补齐：`/` 双语过滤与键盘选择、块拖动/移动/复制/多选、大纲与标题折叠、`[toc]` 实时目录、查找替换与字数统计、图片/附件导入与可恢复清理、图片显示宽度、代码语言/高亮/复制、离线 Mermaid、KaTeX 行内/块公式、脚注、英文拼写检查，以及独立 HTML / PDF 导出与 PDF 打印预览。工具入口为格式栏“正文编辑与导出”；Ctrl+H 打开替换。公式可通过工具菜单或 `/` 插入，双击公式编辑。图片/表格宽度保存在应用偏好中，不扩展 Markdown 语法。折叠仅影响编辑视图，导出会展开全文。附件清理先等待正文保存，再检查磁盘引用，移入专用回收区，可恢复。

`web/editor/editor.js` 负责上游插件配置与命令适配；`qml/SourceEditor.qml` 通过 WebChannel 传递主题、焦点与工作区位置；`src/EditorSessionWeb.cpp` 缓存已提交 Markdown，并按文档代次和递增序号拒绝过期消息。现有 `EditorSession::execute/link/undo/redo` 和 controller 存储接口保留。切换笔记、切库和关闭前先向编辑区取得最新已提交状态；自动保存回包不重建编辑器或撤销栈。

只打开不编辑时保留原文，未知 front matter 原样保留；编辑后由上游标准 Markdown 序列化，写法与空行可能规范化。尚未支持的 HTML 扩展保护为只读，不静默丢弃。所有脚本、样式随应用打包，无 CDN、运行时 npm 或 Node 依赖；页面不允许网络请求和外部导航，本地图片仅可读取当前笔记库中的文件。

构建脚本和依赖更新只在虚拟机执行：

```sh
cd web/editor
npm ci --ignore-scripts
npm run build
```

精确依赖及完整性哈希在 `package-lock.json`，对应第三方许可在 `THIRD_PARTY_LICENSES.txt`；打包后的 `dist/editor.js`、按需加载的 `dist/diagram.js`、公式字体和英文词典随源码提交。维护 bundle 时同时更新锁定文件与许可清单。Qt WebEngine 的 helper、resources、locales 及适用 Qt/Chromium 许可材料也须随正式部署收集，不能只复制应用可执行文件。

源码视图使用 CodeMirror 6 的可见区域渲染，文本、选区和撤销仍同步到原有 ProseMirror 会话。`npm run build` 经 `build.mjs` 打包，并对锁定的 Milkdown 7.23.0 序列化器应用 `transformer-patch.mjs`：消除段落数组的平方级复制、复用 schema 候选表。补丁不改写 `node_modules`，上游源码不匹配时构建直接失败；升级依赖须审查此补丁并在 VM 重跑 `tests/TransformerRegression.mjs` 和编辑回归。

预览用 `nextnotes --settings-dir /absolute/path/to/preview-settings` 隔离应用设置（内部为 `NextKde/NextNotes.conf`），不覆盖 `XDG_CONFIG_HOME`，避免 fcitx 客户端读不到系统主题。输入法组合期间不导出 preedit、执行格式命令或完成切换；提交后继续保存。

虚拟机 `kos-vm-dev` / Qt 6.10.2：repository、controller、editor-session 三组测试通过；编辑回归覆盖段落与标题输入、行内格式、列表续项退出、任务勾选、引用、代码块、直接输入/粘贴表格、增行增列、列对齐、删除/撤销重做和 front matter 保留。真实 fcitx 回归覆盖组合不提前保存、选词提交、取消组合与选区替换。下方 M1 段落为历史记录，不代表当前内核边界。

手工浏览器回归脚本为 `tests/WebEditorRegression.mjs` 和 `tests/AdvancedEditorRegression.mjs`，仅在 VM 启用开发调试端口后使用。明确提供 `NEXTNOTES_TEST_FILE`，且在应用中打开同一个可丢弃测试笔记；测试会替换该笔记正文。测试笔记须以 `---\ncustom: untouched\n---\n` 为 front matter。设置 `NEXTNOTES_CDP_URL` 指定本地调试端点，`NEXTNOTES_TEST_REPORT` 可保存结果；无自动验证工作流。

## 依赖

Qt 6.8+（Core、Gui、Qml、Quick、QuickControls2、QuickDialogs2、Svg、WebEngineQuick、WebChannel），运行时 Qt Quick Effects、Qt5Compat.GraphicalEffects 与 SVG 图像支持。Kos.Ui 子模块固定为 `608847c9231121b888f11c4e5f84850ac95deda4`，保留上游 GPL-3.0 LICENSE 与 NOTICE.md。应用采用 GPL-3.0-only；第三方组件保留其各自许可证。Flatpak 发布身份为 `org.nextks.notes`，复用 `org.kde.Platform//6.11`。构建及发布流程见 [Flatpak 打包说明](packaging/flatpak/README.md)。

## 仅在指定虚拟机或托管 CI 构建

本机只允许源码阅读、编辑和 Git 操作，不在本机运行以下命令。运行验证前须固定源快照并记录 VM 版本对应关系。

```sh
git submodule update --init --recursive
cmake -S . -B build -DNEXTNOTES_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/nextnotes
```

显式开发覆盖：`-DKOS_UI_SOURCE_DIR=/absolute/path/to/nextkdeAppDemo`。发布/可复现构建使用默认固定子模块，不使用未提交的本地组件 API。测试默认关闭，不添加自动验证工作流，不烘焙 Shader。

编辑区支持 Ctrl+B 加粗、Ctrl+I 斜体、Ctrl+Shift+X 删除线、Ctrl+E 行内代码；侧栏为 Ctrl+Shift+B。格式菜单与快捷键调用同一会话。当前命令是局部源码标记转换，尚非完整 Markdown 语法解析。


## M1b 使用与恢复（早期阶段记录，当前能力以上文为准）

选择一个已存在的本地文件夹；新建文件夹入口可建立库内目录。新建笔记立即进入编辑器，标题从 front matter / 一级标题读取。正文直接编辑原始 Markdown；文件路径重命名不会同步正文标题或链接。搜索标题和正文采用后台子串检索，中文不依赖英文分词；打开命中后选中第一处正文匹配。列表按最近修改排序。

输入停止约 700ms 后保存；切换笔记和关闭窗口等待最新版本保存完成。保存回包不会重置正文、IME 或撤销栈。错误时窗口保留输入，关闭被阻止；可重试、另存为独立新身份副本或打开恢复中心。外部修改在干净状态重读，脏状态保留本地草稿并提示冲突。

恢复中心展示草稿、最近删除及保留提交文件的完整定位信息。重开后存在草稿的笔记先只读，必须明确载入、另存或丢弃，不自动覆盖磁盘。载入时基线已改变则只能另存或明确丢弃重读。另存不删除原恢复记录；切换库如被 pendingDrafts 阻止，确认副本后可在恢复中心显式丢弃原草稿。回收恢复遇到原路径占用或父目录不存在时，可恢复到根目录的新文件名。

窗口大小、栏宽、每笔记光标与滚动位置、字体及外观偏好保存在应用 QSettings 中，不写入正文。窄窗口编辑时收起列表，以“笔记”返回；800px 以上双栏、1000px 以上可展开文件夹侧栏。Ctrl+N 新建、Ctrl+S 保存、Ctrl+F 当前正文查找、Ctrl+Shift+F 全局搜索；编辑快捷键只在正文焦点且不处于输入法组合时启用。

新增 `notes-controller-test` 的异步状态机测试和 repository 文件夹/中文搜索/跨目录移动/重开用例仅已编写，尚未在本机或 VM 执行。需要指定 VM 构建并验证 QML 加载、IME、保存失败、重开恢复、窗口布局和材质。后台搜索当前全库扫描，不宣称 1 万篇性能达标；提交旧版本目前保留并提供定位，尚无自动清理或完整历史版本管理。

本轮补充：切库先准备元数据和首次扫描，全部成功才提交库锁及代次；失败保留旧库读取、保存与关闭能力。启动恢复已授权最近库与稳定 ID 对应笔记，恢复草稿读取完成前不自动打开笔记。撤销/重做使用平台 StandardKey，格式菜单也提供入口（Linux 重做绑定以平台为准）。列表显示逻辑修改时间及当前保存/冲突状态。错误说明使用中文，不直接展示内部错误码和诊断 detail。上述改动与新增回归测试仅编写，须在冻结快照对应 VM 构建及运行，不代表 GUI 或性能验收。

### M1 恢复与身份补充（实现待 VM 验收）

恢复中心以文件名、记录时间及保存前/本次待保存版本展示预览；无法确认的版本禁用另存。`recoveryVersion {token, version: before|pending}` 重新读取库内提交记录，校验目标/同目录 staging 路径与 SHA-256，返回 `text`、`bytes`、`hash`；`recoverCommit` 使用同一校验再生成新身份恢复副本，不覆盖原文件，保留记录和草稿。外部改变后不匹配任何记录摘要的版本拒绝恢复。恢复副本只调整身份字段，不整文重排。

允许源码编辑唯一的 nextnotes_id：保存成功后同步当前选择、最近笔记及位置键；重复 ID 仍拒绝保存。另存成功立即记录新文档，直接关闭重开可恢复副本；移动后通过读取新路径激活文档。外观使用 LiquidSelect、SettingsSwitch 与普通材质 LiquidSlider。

新增控制器身份/另存重开测试，并将真实子进程中断提交测试扩展为经恢复 API 读取两版本、独立字节核对及另存、损坏记录拒绝。仅源码实现，未在本机运行构建、测试或 GUI；须在虚拟机验收，不替代尚缺的 IME、键盘、主题及规模证据。

完整空间备份／恢复与可停止的分批导入见 [备份与导入说明](docs/backup-import.md)，实际交互检查范围见 [审查记录](docs/ui-interaction-audit.md)。AI 索引暂不实现。

当前界面入口以 [以笔记为中心的交互说明](docs/notes-interaction-model.md) 为准：日常笔记操作留在主界面，外观、数据与高级管理集中到设置（Ctrl+,）。

产品视角的进一步调整与 VM 人工记录见 [产品交互复查](docs/product-interaction-review.md)：全局搜索、关联、标题、分类切换和临时编辑返回，以及当前笔记输出与全库设置的分层。
