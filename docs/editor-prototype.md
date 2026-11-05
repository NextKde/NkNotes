# M2a 编辑器比较原型（修复后待虚拟机验证）

独立入口 `prototypes/editor/CMakeLists.txt`，未接产品存储、磁盘保存或业务依赖，D02 尚未选型。两个独立 buffer、各自一条保留的 undo 栈，同一 UTF-8/LF fixture；切换 pane/mode 不重建 state/doc。Load fixture / Load ~1 MB 是一次可撤销全文替换，明确会改变当前候选内容。fixture 包含中文、组合 emoji、NBSP、未知元数据/扩展、注释和恶意脚本文本。

原生直接编辑 QTextDocument，QSyntaxHighlighter 仅标题和正则粗体，不隐藏标记，不识别围栏代码中的伪粗体，不能称完整即时编辑器。导出 `toRawText` 的段落分隔符映射为 LF。Web 以 CodeMirror `state.doc` 为唯一编辑正文，Lezer Markdown/GFM 提供局部装饰；非活动行可隐藏标记，但围栏代码完整排版、表格、图片、链接导航、富文本粘贴转换均未实现。两者均无文件字节往返保证；当前契约为 UTF-8/LF，CRLF/BOM/混合换行/无末尾换行/字面 U+2029 的文件适配另行验证。

本轮针对 review-011 两个 fail 的源码修复：Web 装饰在每个事务先 `DecorationSet.map(update.changes)`，组合期间不重建装饰；`compositionend` 后等待 CodeMirror composing 清除再强制重算。组合期间格式/QA load/select 拒绝，模式请求排队最后一次，退出组合才应用；切换候选拒绝进行中的组合。`snapshot.source` 在组合中返回 null，`draftSource` 是仅供观察的含 preedit 文本，`committedSource` 是组合开始前的检查点，不能把 draft 当已提交保存数据。该方案仍需真实 fcitx 全选替换回归，不能因为源码修复宣布 AC04 通过。

Web 由宿主实际 AppTheme.window/text/accent/accentText/mutedText/border、AppTheme.dark、Application.font.family 和 QFontInfo 的实际像素字号驱动 CodeMirror theme。绑定实时同步系统/手动深浅色与字号；去除网页硬编码浅色和默认 monospace 覆盖。QML 保留共享 KosButton；Flow 工具栏换行，窗口最小 640×480。操作成功后同时 `WebEngineView.forceActiveFocus` 和 DOM focus，原生返回 TextArea 焦点；按钮仍使用共享键盘语义。深浅色、最小宽度、Tab/Enter/Space 与焦点回正文均待 VM。

`commands.json` 是工具栏顺序、格式定界符和 Ctrl+B/I/Z/Shift+Z/Y 的同源表，C++ 载入给 QML，再传 Web；两端快捷键均调用同一候选 command。原生接收表内 marker，不再重复硬编码格式表。这里只承诺 Linux Ctrl 映射，跨平台修饰键未验收。格式为包围定界符，不是智能 toggle/混合格式状态，当前暴露命令不等于完整 AC05/09。

## 已实际观察的 011 结果（不是本轮结果）

原始证据目录 `/home/amao/.local/state/ui-spec-workflow/895040582957/notes-20261104/`，以 `review-011.json`、`gui-steps-011.md`、`editor-build-metadata011.json` 和各 `editor-*.json` 为准。011 VM：Ubuntu 26.04、Qt 6.10.2、Wayland、fcitx5/pinyin、Node 22.23.4；构建成功、171 项源码哈希匹配，但首段构建输出不完整。

- 两端 fixture 初始读回一致；真实反向选区 362/0 经 bold 为 364/2，一次 undo 恢复原文；Web redo 恢复。原生 undo 后选区折叠为 0/0，是待评估差异。
- Web 已测 source/live 原文与选区保留，但记录全部 scroll=0；非零滚动和原生 mode 矩阵未测。
- 真实中文长句提交/部分撤销有成功证据；Web 全选 fixture 后输入 `ceshi` 出现 Position 91 超出 length 1/2/4/5 的 RangeError，随后 TypeError。这是确定失败，本轮修复后待重测。
- 同一 1,049,055 字节文档两端粘贴后 exact=true。原生 3128.98ms、Web 280.18ms 只是各一次且 AT-SPI/CDP 读回边界不同，不是可比较性能结论。
- fetch 被 CSP 拒绝、scripts 仅应用 bundle；security JSON 的 images=24 与后续步骤空数组不一致，不能宣布安全边界完整通过。

## VM 构建与操作

仅主控冻结新快照后在 `kos-vm-dev` 执行，生成者本机没有构建/npm/测试/启动或图形验证。lock 已在 011 VM 生成并纳入源码，使用 npm ci，不再生成锁。复用已配置的外置 CMake cache，其 Qt 6.10.2 WebEngine 私有 devprefix 路径已配置，不改用旧文档的 root/usr cmake 路径：

```sh
export PATH=/home/amao/apps/nextnotes-editor-tools/node-v22.23.4-linux-x64/bin:$PATH
cd /home/amao/apps/nextnotes/prototypes/editor/web
npm ci
npm run build
cmake --build /home/amao/apps/nextnotes-editor-build -j2
/home/amao/apps/nextnotes-editor-build/editor-comparison
```

源码新增 commands.json 资源，正常 CMake 自动重新配置应更新资源；若需显式配置，执行 `cmake -S /home/amao/apps/nextnotes/prototypes/editor -B /home/amao/apps/nextnotes-editor-build`，保留现有 cache。先核对 CMAKE_HOME_DIRECTORY 指向本次冻结副本；错误路径不能继续沿用。记录源码/lock/bundle/可执行文件哈希与完整日志，在普通 VM 图形用户会话运行，不关闭 sandbox。CDP 仅测试时绑定 loopback，不能进入产品发布配置。

## 有限 QA 与多样本测量入口

工具栏可直接加载同 fixture / 约 1 MiB（`fixture.repeat(ceil(1048576/fixtureUtf8Bytes))`），不再需手动 clipboard。此 load 测的是事务替换；真实粘贴仍须单测。下列均为 VM CDP 对 `qrc:/editor/web/index.html` 页面求值的 JavaScript；异步宿主结果存 `window.qaResults[id]`，每次用唯一 id，等待该项存在后读取。无需扫描 AT-SPI 全树：

```js
window.qa.host({id:"native",op:"pane",web:false}) // 切到原生；true 切 Web，返回 ok
window.qa.host({id:"large",op:"measure",operation:"load-large",count:5})
window.qaResults.large // 未完成为 undefined；完成为摘要或 error
window.qa.host({id:"selection",op:"select",anchor:2000,head:1000,scroll:900})
window.qa.host({id:"before",op:"snapshot"})
window.qa.host({id:"mode",op:"measure",operation:"mode",count:6})
window.qa.host({id:"after",op:"snapshot"})
window.qa.host({id:"dark",op:"theme",dark:true})
```

`measure.operation` 支持 `load-fixture`、`load-large`、`mode`（每次切换）和表中命令 bold/italic/strike/code/undo/redo，count 限 1–20，默认 5。同一时刻只允许一组测量；不含自动预热，每个样本都修改当前独立 buffer/undo 栈。测格式时应先设置已知选区；连续 undo/redo 可能耗尽历史，测量者必须核对结果。输出原始 samplesMs、n、最近秩 p50Ms/p95Ms、engine、operation、boundary，同时打印 `QA_MEASURE` JSON。错误/超时必须保留，不能算成功样本；主控在 30 秒未获结果时记录超时，检查进程日志。

统一边界是宿主单调时钟 dispatch 开始，到当前编辑 pane 的异步 Qt `grabToImage` 回调完成。Web 先经过事务应答和两次 requestAnimationFrame 布局检查点，原生先 Qt.callLater；两端最终都等待同一种 Qt 场景捕获。数值含 bridge、布局等待和截图开销，是可重复的“操作到场景捕获完成”探针，不是纯编辑器耗时或显示器实际呈现延迟；Web compositor 可能仍有独立流水线，应结合截图内容/帧记录确认，不能仅凭此声明帧性能。窗口必须可见，比较用相同窗口尺寸和 VM 状态。RSS/GPU/冷启动及进程隔离仍另行测量；本程序始终链接 WebEngine，原生 tab 下总 RSS 也不是原生独立成本。

Web 直接接口：`window.snapshot()`（source/committedSource/draftSource/composing/live/pendingLive/ranges/scroll），`window.qa.load(string)`，`window.qa.select(anchor,head,scroll)`，`window.setLive(bool)`，`window.command(id)`，`window.qa.ready()`（双 rAF 后 snapshot Promise）。load/select/command 返回是否接受；setLive 在组合中返回 false 但排队。原生宿主 snapshot 返回 source/composing/anchor/head/scroll/live。select 使用 UTF-16 索引，设非零 scroll 只帮助模式映射回归，不能用注入 composing 状态代替真实 IME。

新快照必须覆盖：真实 fcitx 全选替换 ceshi、长句候选切换/文中替换/标点/撤销、组合期间模式点击；两端反向选区和非零 scroll 模式前后 source/undo/redo；最小窗口/键盘工具栏/格式后直接物理输入；深浅色动态变化；实际 DOM/网络外部图片、HTML粘贴、本地URL和弹窗边界。当前无通过声明。

## 依赖、许可与部署取舍

Web direct 包精确版本与完整 package-lock 均已存在。011 `editor-build-metadata011.json` 记录了 CodeMirror state/view/commands/language/lang-markdown，以及传递的 autocomplete/lint、HTML/CSS/JS language、Lezer 解析器和 crelt/style-mod/w3c-keyname 等包版本与 MIT 声明；esbuild 和平台包也是构建工具而非目标机 Node 运行依赖。lock 中存在传递的另一份 state/view 版本，须在打包/升级时检查是否形成重复运行实例，不能用 manifest 六个包代替完整依赖审计。MIT 包仍需保留实际版权和 LICENSE；[CodeMirror 源仓库](https://github.com/codemirror/view)、[Lezer 源仓库](https://github.com/lezer-parser/markdown)、[esbuild 源仓库](https://github.com/evanw/esbuild) 为上游来源，发布以锁定版本实际包文本为准。

两路线共有 Qt Core/Gui/Qml/Quick/QuickControls2/QuickDialogs2/Svg、QML imports/platform plugins 和 pinned Kos.Ui（仓库 LICENSE GPL-3.0）。原生另依赖 QTextDocument/高亮/文本布局（Qt Gui），没有 Node 或 Chromium 必需性；目前没有单独原生构建，不能给它虚构精确二进制/RSS 数字。嵌入额外包含 WebEngineQuick/Core、WebChannel 等实际动态依赖、QtWebEngineProcess、Chromium resources/locales、ICU/V8 数据（依 Qt 构建选项）、应用 JS bundle。多进程、沙箱和 Chromium 安全更新增加部署维护成本；原生则需自研标记隐藏、代码上下文和复杂即时布局，当前可见能力差距更大。

011 实际 bundle 910,150 B、双内核 executable 14,156,720 B、WebEngineCore 207,184,744 B（约 198 MiB）。主控补充 VM 只读清单：resources 约 13 MiB、locales 约 40 MiB、QtWebEngineProcess 磁盘占用约 16 KiB。仅这些 WebEngine 主要组件已约 251 MiB，再加共有 Qt/QML/系统依赖；不含压缩、裁剪或全发布闭包，不能说“Web 仅增加 0.9 MB”。原生省去此组主要 WebEngine 组件的潜在成本，但完整独立打包差额待 M5 实测。

[Qt WebEngine 许可说明](https://doc.qt.io/qt-6/qtwebengine-licensing.html) 区分 Qt 部分的商业/LGPLv3/GPL 选项和 Chromium 多种第三方许可，不能笼统称 MIT。网页当前重定向开发版资料，只用来说明组成，6.10.2 的权威归档必须来自 VM 安装包 `/usr/share/doc/libqt6webenginecore6/copyright` 和对应源码 LICENSE/第三方声明。需保留版权、许可和适用源码/修改/重新链接等义务材料；具体适用路径由最终产品许可与分发方式决定。本原型无商业许可结论。

[Qt 部署说明](https://doc.qt.io/qt-6/qtwebengine-deploying.html) 列出 helper、resources、locales 和路径配置。正式 M5 完整闭包需逐架构归档 Qt/Chromium/Kos.Ui/npm 的许可证、源码来源/构建配置、全部运行依赖与实际体积，并验证目标机离线启动和沙箱。D02 当前可理解的取舍是：Web 有现成语法/编辑事务能力但增加百 MiB 级 Chromium 组件与升级义务；原生部署可减去该组组件但即时编辑需明显更多实现。两者关键 IME/模式/性能矩阵尚未完成，不能据此选型。

## 外部内容边界

WebEngine interceptor 只允许 qrc:/editor/，导航只允许固定入口，CSP 禁网络/图片/frame/object/form/connect。正文不是 HTML DOM，不使用 innerHTML；受信任 bundle 执行，用户 Markdown 不执行。QML bridge 参数经 JSON.stringify，QA 仅此独立开发宿主，无产品存储桥接。组合安全、离线边界和许可证闭包都必须凭新快照的实际证据，不能从源码推断验收通过。
