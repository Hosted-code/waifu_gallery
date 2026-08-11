# Waifu Gallery — 参考资料与设计备忘

> 综合自 ChatGPT、Gemini 的讨论及 TagStudio/Eagle 的功能分析，提取高价值建议。

---

## 一、技术栈决策

### 核心结论：功能先行，后端/UI 解耦，UI 分叉后做

- **现阶段**：继续 Qt6 Widgets，不花精力美化，够用即可
- **所有新功能**：代码写在 core 层，与 UI 解耦，确保未来换 UI 只需重写表现层
- **功能成型后**：做 QML vs Tauri + React + shadcn/ui 原型对比，选视觉更好的

### 为什么不现在换 Tauri

1. 重度图片应用经 WebView → JS → IPC → Native 链路会有性能顾虑（大量缩略图、缩放平移、GPU 渲染）
2. 已有 C++ 代码资产（数据库、解析器、导入器、标注器、图片加载器），换 Tauri 等于重做整个应用
3. 功能开发阶段 90% 是后端逻辑，UI 层很薄，换技术栈的收益暂时不大

### 为什么最终可能选 Tauri

1. Web UI 生态（shadcn/ui + Tailwind）让非设计师也能做出高端感
2. AI 生成 UI 的参考和生态远强于 QML（v0.dev / Bolt.new 可直接生成 React 组件）
3. Eagle 本身就是 Electron 证明 Web UI 可行
4. 瀑布流、毛玻璃、微交互、过渡动画在 Web 生态开箱即用

---

## 二、UI 设计方向

### 风格定位

参考 **Eagle + Lightroom + VS Code**，而非二次元网站风格：

- 现代、克制、高信息密度
- 深色主题为主
- 动画少而精
- 键盘操作优先，鼠标操作为辅
- **不要**花哨装饰（🌸✨💕🎀），主体是生产力工具

### 三栏布局

- **左侧**：Sidebar（标签树/智能文件夹/收藏夹），宽 220-260px
- **中间**：自适应图片网格（UI 分叉后改为瀑布流 Masonry）
- **右侧**：Metadata Inspector（标签/元数据/评分），宽 280-320px，可折叠

### 沉浸式图片查看器

- 按 Enter / Space / F11 进入全屏查看
- 鼠标移到边缘才出现控制栏
- 缩放到 100% 时显示分辨率和缩放比例
- 预加载相邻图片（N-1, N+1）保证切换流畅

### 色板参考

```
Background       #15171C
Surface          #1D2027
Surface Hover    #252933
Border           #30343D
Primary          #8B5CF6
Secondary        #EC4899
Accent           #38BDF8
Text             #F5F7FA
Text Secondary   #9CA3AF
```

---

## 三、标签体系设计

### 标签作为富对象（参照 TagStudio）

标签不只是字符串，而是有属性的实体：

- **主名称** + **缩写**（如 FNAF = Five Nights at Freddy's）
- **别名列表**（结城明日奈 / 亚丝娜 / Asuna）
- **父标签**（多继承，如 亚丝娜 → Sword Art Online + 角色）
- **分类标记**（is_character / is_attribute）
- **颜色**（按分类着色，如角色红色、属性蓝色、作品绿色）
- **隐藏标记**（标记为隐藏的标签不参与默认搜索）

### 父子级标签（TagStudio 核心设计）

- 子标签"IS-A"父标签，搜索父标签自动找到所有子标签
- 传递性：Shrek → Character → Fictional，搜索 Fictional 也能找到 Shrek
- 消歧义：同名标签通过父标签区分，如 Freddy (FNAF) vs Freddy (Scooby-Doo)
- 数据库用 `tag_parents` 表 + 递归 CTE 查询实现

### 同义标签（别名）

- 搜索任意别名自动匹配主标签
- 添加标签时自动归一化到主标签
- 对二次元场景极其关键：角色名/日文名/英文名/中文名/昵称统一

### 标签浏览器

- 标签管理从侧边栏附属功能升级为独立全屏视图
- 按分类（角色/属性/作品/画师）浏览标签，每个标签显示图片数量
- 拖拽标签建立父子关系
- 标签搜索 + 自动补全

### 标签自动补全

输入 `blu` 立即列出：

```
blue_hair          12,321
blue_eyes           8,923
blue_dress          2,341
blue_background     1,823
```

并显示别名和父标签：

```
blue_hair
├── Alias: aqua_hair
├── Parent: hair_color
└── Related: long_hair
```

---

## 四、Tag Query Language

搜索应成为软件核心能力，支持结构化查询：

```
character:初音ミク AND blue_hair AND -commission AND rating:>=4
```

基础语法：

| 语法 | 含义 |
|---|---|
| `tag:xxx` | 包含标签 |
| `-tag:xxx` | 排除标签 |
| `rating:>=4` | 评分筛选 |
| `favorite:true` | 仅收藏 |
| `color:blue` | 颜色搜索 |
| `width:>=2000` | 分辨率筛选 |
| `date:2026` | 日期筛选 |
| `AND` / `OR` / `NOT` | 逻辑组合 |
| `(...)` | 嵌套分组 |

---

## 五、AI 标签审核工作流

AI 只出候选标签，人负责确认：

```
AI Tagging Result

1girl          ██████████ 99%
blue_hair      █████████  96%
smile          ████████   91%
school_uniform ██████     74%
outdoors       █████      63%
```

- 按置信度排序显示
- 支持 `[Accept ≥ 90%]` 一键批量确认
- 审核结果才写入数据库，AI 不直接修改标签

---

## 六、EXIF / AI 生成元数据解析

二次元图片大量来自 AI 生成工具，解析其元数据是刚需：

**可解析字段：**
- Prompt / Negative Prompt
- Steps / Sampler / CFG / Seed
- Model / LoRA / VAE
- Size / Generation time

**进阶能力：**
- 点击某个 LoRA → 找出所有使用该 LoRA 的图片
- 点击某个 Model → 找出所有使用该 Model 的图片
- 让软件从"图片管理器"变成"二次元创作素材管理器"

---

## 七、图片去重三层次

| 层级 | 方法 | 场景 |
|---|---|---|
| 精确重复 | xxHash（已有） | 完全相同的文件 |
| 近似重复 | pHash / dHash / aHash | 裁剪/压缩/缩放后的相同图片 |
| 语义相似 | feature_hash（已有） / CLIP embedding | 构图或内容相似的图片 |

---

## 八、二次元专属功能

### Boss Key / R18 隔离库

- 快捷键一键隐藏/锁定 R18 内容
- 密码解锁隔离库
- 二次元图片管理刚需

### Danbooru / Pixiv 标签自动抓取

- 通过网络 API 或离线词库（EhTagTranslation）自动填入中/英/日多语种标签
- 输入 Pixiv ID 即可自动获取标签和画师信息

### 动图帧预览

- GIF / APNG / WebP 悬停播放预览
- 图片查看器中逐帧查看（Frame-by-frame）

### 标签包生态

- 社区共享标签体系（Danbooru / Gelbooru / Safebooru 词库）
- 导入后自动建立层级和别名关系
- 可形成"二次元标签词典"生态，这是项目最有辨识度的方向之一

---

## 九、架构演进

```
src/
├── core/               ← 纯 C++ 后端，无 Qt UI 依赖
│   ├── database/       SQLite 操作、schema 管理
│   ├── tag_system/     标签 CRUD、父子关系、别名、消歧义、递归搜索
│   ├── search/         标签搜索、文本搜索、高级查询语言
│   ├── importer/       多线程导入、元数据解析
│   ├── image_io/       图片解码、缓存策略、缩略图生成
│   └── metadata/       EXIF 读取、AI 生成元数据解析
│
├── gui_qt/             ← 当前 Qt Widgets UI（维持现状，够用即可）
│
└── gui_web/            ← 未来：Tauri + React 前端（功能成型后评估）
```

### 数据库新增表

```sql
-- 标签父子关系（多继承）
CREATE TABLE tag_parents (
    parent_tag_id INTEGER NOT NULL,
    child_tag_id  INTEGER NOT NULL,
    PRIMARY KEY (parent_tag_id, child_tag_id),
    FOREIGN KEY (parent_tag_id) REFERENCES tags(tag_id) ON DELETE CASCADE,
    FOREIGN KEY (child_tag_id)  REFERENCES tags(tag_id) ON DELETE CASCADE
);

-- 标签别名
CREATE TABLE tag_aliases (
    tag_id     INTEGER NOT NULL,
    alias_name TEXT NOT NULL UNIQUE,
    PRIMARY KEY (tag_id, alias_name),
    FOREIGN KEY (tag_id) REFERENCES tags(tag_id) ON DELETE CASCADE
);

-- 收藏/评分
CREATE TABLE favorites (
    pic_id  INTEGER PRIMARY KEY NOT NULL,
    rating  INTEGER DEFAULT 0,
    FOREIGN KEY (pic_id) REFERENCES pictures(id) ON DELETE CASCADE
);

-- 智能文件夹
CREATE TABLE smart_folders (
    folder_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    query_json  TEXT NOT NULL
);
```
