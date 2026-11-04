# M2a 编辑器比较原型（待虚拟机验证）

独立入口 prototypes/editor/CMakeLists.txt。未连接产品存储或修改业务依赖。两个独立 buffer 使用同一 UTF-8 fixture：中文、组合 emoji、真实 NBSP、未知 front matter、注释、GFM、未知扩展、恶意脚本文本和远程图片文本。源码检查按钮把全文、UTF-16 anchor/head 和滚动位置输出到日志（仅使用测试数据）。

原生候选直接编辑 QTextDocument，QSyntaxHighlighter 只覆盖显示格式，当前仅标题和粗体。它不会真正隐藏或替换 Markdown 标记，不能称为完整即时编辑器。导出用 toRawText 并将段落分隔符映射为 LF，避免 toPlainText 将 NBSP 转为空格。当前输入契约为 UTF-8/LF，不承诺 CRLF/BOM 或字面 U+2029 的文件字节保真。

嵌入候选将 CodeMirror state.doc 作为唯一正文，Lezer Markdown/GFM 语法树提供局部样式及非当前行标记隐藏。模式切换仅 reconfigure 装饰，不替换 state 或撤销栈；当前行、选区所在行显露语法。不是 HTML 编辑器，不解析用户 HTML 为 DOM，不使用 HTML 序列化保存。未实现完整块替换、表格编辑、图片、链接导航和富文本粘贴转换。

共用 QML 格式入口（粗体、斜体、删除线、行内代码）插入定界符，一次命令一次撤销；不宣称智能 toggle 或混合格式状态。保留反向选区方向。Ctrl+B/I/Z 与对应工具栏调用同一候选命令。CodeMirror 格式命令支持多个 range；原生 TextArea 仅单 range。原生组合期间拒绝工具栏改写；嵌入组合期间拒绝格式命令并暂缓装饰更新，实际 IME 行为仍需验收。源码切换保留原生控件实例；嵌入保存并恢复 scrollTop，布局变化后的大致滚动稳定性待测。工具栏使用 pinned Kos.Ui 的普通 KosButton，不新增液态浮层；主窗口避开 KosApplicationWindow 的 Ctrl+B 侧栏绑定。网页颜色目前是对照原型固定浅色，尚未完成 AppTheme 深浅色同步，不作为 mac27 成品视觉验收。

## 离线边界和依赖

WebEngine interceptor 仅允许 qrc:/editor/，导航仅允许固定入口；CSP 禁止网络、图片、frame、object、form 和连接。可执行脚本仅为资源内打包的受信任应用代码。用户 Markdown 中 script 和 URL 均为文本；未设置 innerHTML，未注入用户脚本，未启用远程访问或关闭 sandbox。QML 命令参数通过 JSON.stringify 编码；bridge 当前使用 runJavaScript，实际往返/焦点延迟待测。必须在 VM 验证拒绝网络请求、导航、弹窗和用户脚本。

package.json 精确固定直接依赖版本。生成阶段没有执行 npm；package-lock.json 尚未生成，传递依赖未冻结。主控冻结源码后在 VM 生成 lock 并复核 registry、版本、integrity、许可，将 lock 纳入下一次证据快照；此后使用 npm ci。不得将当前 manifest 称为可重现的最终依赖闭包。

CodeMirror、Lezer、esbuild 为 MIT 项目，最终以 lock 对应实际包 LICENSE 复核并保留。Qt 模块及 Chromium 的许可/第三方声明须按实际部署版本归档；Kos.Ui 固定源码为 GPL-3.0。分发需包含 QtWebEngineProcess、resources、locales 和所需 Qt/QML 模块，部署体积不是 npm bundle 大小；本原型不作产品许可选择。

参考：[Qt 请求拦截](https://doc.qt.io/qt-6/qwebengineurlrequestinterceptor.html)、[CodeMirror 装饰](https://codemirror.net/examples/decoration/)（本轮该站读取返回 403；API 最终以 VM 安装的声明文件和运行结果核对）。

## 仅在 kos-vm-dev 执行

从冻结的源码副本根目录执行，记录 commit、所有源码与 lock 的 SHA-256。不要在维护者本机执行：

```sh
export PATH=/home/amao/apps/nextnotes-editor-tools/node-v22.23.4-linux-x64/bin:$PATH
cd prototypes/editor/web
npm install --package-lock-only --ignore-scripts
npm ci
npm run build
cd ../../..
cmake -S prototypes/editor -B prototypes/editor/build \
 -DQt6WebEngineQuick_DIR=/home/amao/apps/nextnotes-editor-tools/root/usr/lib/x86_64-linux-gnu/cmake/Qt6WebEngineQuick \
 -DQt6WebEngineCore_DIR=/home/amao/apps/nextnotes-editor-tools/root/usr/lib/x86_64-linux-gnu/cmake/Qt6WebEngineCore \
 -DQt6WebEngineCoreTools_DIR=/home/amao/apps/nextnotes-editor-tools/root/usr/lib/x86_64-linux-gnu/cmake/Qt6WebEngineCoreTools \
 -DQt6WebChannel_DIR=/home/amao/apps/nextnotes-editor-tools/root/usr/lib/x86_64-linux-gnu/cmake/Qt6WebChannel \
 -DQt6WebChannelQuick_DIR=/home/amao/apps/nextnotes-editor-tools/root/usr/lib/x86_64-linux-gnu/cmake/Qt6WebChannelQuick \
 -DQt6Positioning_DIR=/home/amao/apps/nextnotes-editor-tools/root/usr/lib/x86_64-linux-gnu/cmake/Qt6Positioning \
 -DQt6PositioningQuick_DIR=/home/amao/apps/nextnotes-editor-tools/root/usr/lib/x86_64-linux-gnu/cmake/Qt6PositioningQuick
cmake --build prototypes/editor/build -j2
./prototypes/editor/build/editor-comparison
```

在 VM 图形用户会话运行，不以 root 启动、不关闭 sandbox。缺失模块、CMake/编译/API/运行错误均需记录并修复后重新固定快照；本文没有编译成功声明。

## 验证矩阵（全部待执行）

- AC04：两候选中文长句 preedit、候选切换确认、全角标点、连续撤销；记录输入法版本、屏幕录像和中断情况。
- AC05/06：fixture 未编辑导出逐字符比较；反向选区跨 emoji/NBSP，格式一次撤销、redo、模式切换，再比较 source/anchor/head。关注原生 undo 返回 cursor 是否满足期望。
- AC09：Tab 到工具栏、Enter/Space 执行、恢复焦点、快捷键、输入法候选不遮挡。斜杠菜单/弹层尚未实现，不能据原型判 AC09 全通过。
- AC14：使用同一 fixture 重复至约 1MB，记录精确 UTF-8 字节数；通过各候选全选粘贴载入（加载本身与粘贴分别计时），测输入/undo/模式切换、帧延迟和 RSS。原型暂未提供自动 1MB load 按钮。记录宿主及所有 WebEngine 子进程 RSS、Qt/Node/系统/GPU、冷启动/热启动、多轮样本及 p50/p95；1万笔记和中文搜索不属于本 harness。
- 部署：记录 bundle、QtWebEngineProcess/resources/locales 及总依赖实际大小；验证无网络和用户脚本执行。
- 对比仅能依据实际日志/截图/测量结果。当前 D02 未选型，两个候选均未通过运行验收。
