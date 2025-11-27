# JoPDF Reader

一个基于 Qt 和 MuPDF 的现代化 PDF 阅读器，采用模块化架构设计，目标是提供流畅的阅读体验和强大的功能。

## ✨ 主要特性

### 📖 阅读功能
- **多种显示模式**：单页、双页、连续滚动
- **灵活缩放**：适应页面、适应宽度、自定义缩放（25% - 400%）
- **多标签页**：同时打开多个 PDF 文档

### 🔍 交互功能
- **全文搜索**：支持大小写敏感、全字匹配(目前是当前页，测试功能OK后做全文搜索)
- **文本选择**：字符级、单词、整行选择
- **链接支持**：内部跳转和外部链接
- **导航面板**：目录大纲、缩略图预览

### 📝 高级功能
- **大纲编辑**：添加、删除、重命名书签
- **文本预加载**：后台异步提取文本
- **PDF 类型识别**：自动识别文本 PDF 和扫描 PDF

### 🎨 用户界面
- **现代化设计**：参考 PDF Expert 的简洁风格
- **响应式布局**：自适应窗口大小变化
- **快捷键支持**：完整的键盘导航
- **工具栏**：直观的操作按钮和状态显示

## 🏗️ 架构设计

### 核心模块

```
JoPDF
├── Controller 层 (会话管理)
│   └── PDFDocumentSession - 文档会话管理
├── Handler 层 (功能处理)
│   ├── PDFViewHandler - 视图状态管理
│   ├── PDFContentHandler - 内容管理
│   └── PDFInteractionHandler - 交互处理
├── Manager 层 (资源管理)
│   ├── PageCacheManager - 页面缓存
│   ├── TextCacheManager - 文本缓存
│   ├── ThumbnailManager - 缩略图管理
│   ├── OutlineManager - 大纲管理
│   ├── SearchManager - 搜索管理
│   └── LinkManager - 链接管理
├── Renderer 层 (渲染引擎)
│   └── MuPDFRenderer - MuPDF 渲染封装
└── UI 层 (用户界面)
    ├── MainWindow - 主窗口
    ├── PDFDocumentTab - 文档标签页
    ├── PDFPageWidget - 页面显示组件
    ├── NavigationPanel - 导航面板
    └── SearchWidget - 搜索工具栏
```


## 📚 使用说明

### 基本操作

| 功能 | 快捷键 | 说明 |
|------|--------|------|
| 打开文件 | `Ctrl+O` | 打开 PDF 文档 |
| 新标签页打开 | `Ctrl+Shift+O` | 在新标签页中打开 |
| 关闭文档 | `Ctrl+W` | 关闭当前标签页 |
| 上一页 | `PgUp` | 翻到上一页 |
| 下一页 | `PgDown` | 翻到下一页 |
| 首页 | `Home` | 跳转到第一页 |
| 末页 | `End` | 跳转到最后一页 |

### 缩放操作

| 功能 | 快捷键 | 说明 |
|------|--------|------|
| 放大 | `Ctrl++` | 放大显示 |
| 缩小 | `Ctrl+-` | 缩小显示 |
| 实际大小 | `Ctrl+0` | 100% 显示 |
| 适应页面 | `Ctrl+1` | 适应整页 |
| 适应宽度 | `Ctrl+2` | 适应宽度 |

### 搜索功能

| 功能 | 快捷键 | 说明 |
|------|--------|------|
| 打开搜索 | `Ctrl+F` | 显示搜索栏 |
| 查找下一个 | `F3` / `Ctrl+G` | 下一个匹配 |
| 查找上一个 | `Shift+F3` / `Ctrl+Shift+G` | 上一个匹配 |

### 文本操作

| 功能 | 快捷键 | 说明 |
|------|--------|------|
| 复制 | `Ctrl+C` | 复制选中文本 |
| 全选 | `Ctrl+A` | 选择当前页全部文本 |
| 取消选择 | `Esc` | 清除文本选择 |

### 视图切换

| 功能 | 快捷键 | 说明 |
|------|--------|------|
| 导航面板 | `F9` | 显示/隐藏导航面板 |
| 单页模式 | - | 一次显示一页 |
| 双页模式 | - | 一次显示两页 |
| 连续滚动 | - | 连续滚动所有页面 |

## 🔧 核心组件说明

项目采用分层架构，从高到低是UI层、Session层、Handler/Cache/Renderer层、Model/Manager/Tool层，此外还有Util工具包。

目前的技术方案是上层依赖下层，不跨层调用(目标，代码正往这个方向调整)。

### UI层

主窗口是MainWindow，包含菜单栏、工具栏、Tab页。Tab页为PDFDocumentTab,PDFDocumentTab包含导航栏和页面。导航栏为NavigationPanel，包括
大纲和缩略图，大纲为OutlineWidget，缩略图为ThumbnailWidget。页面为PDFPageWidget。

此外，还有一些小组件：SearchWidget、OutlineDialog。

UI层负责界面布局，响应各种事件，接受Session层的信号更新UI。

### Session层

Session层是这个应用最重要的一层。UI层的交互全部委托给Session层，由Session层根据职责分发给不同的Handler，同时UI层接受Session层的信号更新UI。

Session即PDFDocumentSession，主要管理Handler、State、Renderer、Cache，这4者的生命周期由Session负责。

每个Tab页都有一个Session，多个Tab页不共享数据，即Session是隔离的。PDF的核心状态数据维护在Session的State里，State为PDFDocumentState。

### Handler层

Session本质上不干业务重活，负责分发业务给具体的Handler。Handler在应用中真正负责干活。目前划分为3个Handler：PDFViewHandler、
PDFContentHandler、PDFInteractionHandler。

PDFContentHandler负责打开PDF、关闭PDF，同时负责大纲和缩略图数据的加载。

PDFViewHandler负责管理视图状态：页码跳转、缩放、显示模式。

PDFInteractionHandler负责用户交互，比如选择文本、搜索、链接跳转。

### Cache层

Session管理PDF的一些缓存数据。包括文本缓存、页面缓存，文本缓存为TextCacheManager，为搜索、选择文本以及未来的批注功能提供数据支援；页面缓存
为PageCacheManager，渲染好的页面存放到缓存中，提升阅读体验，减少加载页面耗时。

### Renderer层

PDF的底层是有MuPDF提供渲染能力的，Renderer层封装了MuPDF的API，方便其他模块使用。由于MuPDF的context/document不能在多线程中混用，而是每个
线程(主线程/线程池里的线程)使用自己的Renderer，关于这点，还在想办法提升性能。

### Manager层

Handler层只负责处理业务，它不持有状态和数据，状态来自Session的State，数据来自Manager，比如大纲数据由OutlineManager提供，缩略图
由ThumbnailManagerV2提供。还有一些交互功能也由相应的Manager来提供，比如搜索来自SearchManager。

### Model层/Tool层

辅助层，为其他模块提供数据建模，并非MVC或MVVM的Model。同样的，Tool层类似。

## 📊 性能优化

### 缓存策略

1. **Near Current**: 优先缓存当前页附近
2. **LRU**: 最近最少使用淘汰
3. **内存限制**: 可配置最大缓存大小

### 渲染优化

- 按需渲染：只渲染可见页面
- 异步加载：后台线程生成缩略图
- 防抖动：窗口调整大小时延迟重新渲染

### 文本处理

- 懒加载：需要时才提取文本
- 预加载：后台提取未来可能需要的文本
- 缓存：避免重复提取


## 🐛 已知问题


### 代码规范

- 使用 Qt 编码风格
- 添加必要的注释
- 保持模块化设计


## 📄 许可证



## 🙏 致谢

- [MuPDF](https://mupdf.com/) - 强大的 PDF 渲染引擎
- [Qt Framework](https://www.qt.io/) - 跨平台应用框架
- PDF Expert - UI 设计灵感来源

## 📧 联系方式

- 项目主页: https://github.com/techfs2026/JoPDF

## 🌟 Star History

如果这个项目对你有帮助，请给个 Star ⭐️

---

**Made with ❤️ by @techfs**
