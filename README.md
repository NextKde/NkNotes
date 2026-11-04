# NextNotes / NkNotes

面向 NextKde 的原生 Qt Quick Markdown 笔记应用。当前源码推进到 **M1b 真实笔记库流程，等待 VM 验证**：选择单个本地库、三栏导航、源码编辑、700ms 自动保存、中文子串搜索、收藏、文件夹、移动与回收恢复。首次打开不扫描个人目录、不创建演示数据。即时排版、附件、知识关联仍属于后续阶段。

产品依据：[产品规格](docs/PRODUCT_SPEC.zh-CN.md)。实现边界、编辑器比较、命令表、最小文件约定和组件差距：[架构记录](docs/architecture.md)。本轮源码尚未经过构建、运行或视觉验收。

## 依赖

Qt 6.8+（Core、Gui、Qml、Quick、QuickControls2、QuickDialogs2、Svg），运行时 Qt Quick Effects、Qt5Compat.GraphicalEffects 与 SVG 图像支持。Kos.Ui 子模块固定为 `608847c9231121b888f11c4e5f84850ac95deda4`，保留上游 GPL-3.0 LICENSE 与 NOTICE.md。应用正式许可与发布身份尚待确定，不作为当前可分发版本。

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


## M1b 使用与恢复

选择一个已存在的本地文件夹；新建文件夹入口可建立库内目录。新建笔记立即进入编辑器，标题从 front matter / 一级标题读取。正文直接编辑原始 Markdown；文件路径重命名不会同步正文标题或链接。搜索标题和正文采用后台子串检索，中文不依赖英文分词；打开命中后选中第一处正文匹配。列表按最近修改排序。

输入停止约 700ms 后保存；切换笔记和关闭窗口等待最新版本保存完成。保存回包不会重置正文、IME 或撤销栈。错误时窗口保留输入，关闭被阻止；可重试、另存为独立新身份副本或打开恢复中心。外部修改在干净状态重读，脏状态保留本地草稿并提示冲突。

恢复中心展示草稿、最近删除及保留提交文件的完整定位信息。重开后存在草稿的笔记先只读，必须明确载入、另存或丢弃，不自动覆盖磁盘。载入时基线已改变则只能另存或明确丢弃重读。另存不删除原恢复记录；切换库如被 pendingDrafts 阻止，确认副本后可在恢复中心显式丢弃原草稿。回收恢复遇到原路径占用或父目录不存在时，可恢复到根目录的新文件名。

窗口大小、栏宽、每笔记光标与滚动位置、字体及外观偏好保存在应用 QSettings 中，不写入正文。窄窗口编辑时收起列表，以“笔记”返回；800px 以上双栏、1000px 以上可展开文件夹侧栏。Ctrl+N 新建、Ctrl+S 保存、Ctrl+F 当前正文查找、Ctrl+Shift+F 全局搜索；编辑快捷键只在正文焦点且不处于输入法组合时启用。

新增 `notes-controller-test` 的异步状态机测试和 repository 文件夹/中文搜索/跨目录移动/重开用例仅已编写，尚未在本机或 VM 执行。需要指定 VM 构建并验证 QML 加载、IME、保存失败、重开恢复、窗口布局和材质。后台搜索当前全库扫描，不宣称 1 万篇性能达标；提交旧版本目前保留并提供定位，尚无自动清理或完整历史版本管理。
