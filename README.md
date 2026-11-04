# NextNotes / NkNotes

面向 NextKde 的原生 Qt Quick Markdown 笔记应用。当前是 **M0 工程与源码编辑原型**：三栏布局、共享 Kos.Ui 窗口控件、统一格式命令与原生撤销栈。示例内容仅在内存中，退出后不保留；尚未实现笔记库、保存、即时排版和知识关联。

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
