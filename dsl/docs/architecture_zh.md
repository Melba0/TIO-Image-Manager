# 架构说明

[**中文**](architecture_zh.md) ・ [**English**](architecture.md)

本文档描述解释器的模块划分、数据流与关键数据结构，帮助理解代码组织与扩展方式。

## 1. 模块总览

```
src/
├── main.cpp                       # 入口：CLI 解析、依赖装配、REPL、/reload、--json 模式
├── parser/
│   ├── AST.h                      # AST 节点定义（Expr / Quantifier / Filter / Expand / Cnt / Del / ClusterFn ...）
│   ├── Lexer.h / Lexer.cpp        # 词法分析（含 UTF-8 标识符、`:`、`>>`、del 关键字）
│   └── Parser.h / Parser.cpp      # 递归下降语法分析器
├── executor/
│   ├── Context.h / Context.cpp    # 求值上下文：变量表、迭代上下文、标签预筛选、删除回调
│   └── Evaluator.h / Evaluator.cpp# AST 执行引擎（筛选、量词、集合、cnt、>>、del、聚类宏）
├── cache/
│   ├── CacheIndex.h/.cpp          # 缓存索引（cache_index.json，版本 1.2）
│   ├── CacheManager.h/.cpp        # 增量缓存构建/加载/失效、置信度降级、removeImages、applyTagFilters、runClustering
│   └── YoloInference.h/.cpp       # YOLO ONNX 推理 + letterbox 预处理 + 后处理
├── scene/
│   └── SceneInference.h/.cpp      # Places365 场景识别（ONNX）
├── cluster/
│   └── Clustering.h/.cpp          # 基于 embedding 的 DBSCAN 聚类
├── engine/
│   └── OnnxInference.h/.cpp       # ONNX Runtime 后端（InferenceBackend 抽象实现）
├── utils/
│   ├── filesystem_utils.h/.cpp    # 文件系统与时间戳工具
│   └── exif_reader.h/.cpp         # 内置轻量 JPEG EXIF 解析（无需 exiv2）
├── BuiltinMacros.cpp              # 内置宏注册（数学/颜色/直方图/曝光/EXIF/标签/场景/聚类...）
├── ExtensionManager.cpp           # 扩展包扫描、懒加载模型、>> 展开推理、embedding 提取
├── ModelRegistry.cpp              # 模型注册表（models/ 扫描 + registry.json）
└── (include/) 公共头文件
```

## 2. 依赖关系

```
main
  ├── ModelRegistry        （读 models/registry.json + models/base/*/meta.json）
  ├── IsaManager           （读 config/isa_map.json）
  ├── ExtensionManager     （扫 models/extensions、extensions；懒加载扩展模型）
  ├── CacheManager         （依赖 ModelRegistry + ObjectIdGenerator）
  └── Context / Evaluator  （依赖 CacheManager 的 PhotoCache + ExtensionManager）

求值链：Lexer → Parser → AST → Evaluator
推理链：YoloInference（基座）/ ExtensionManager（扩展）→ DetectedObject → Cache / Value
后端：OnnxInference（ONNX Runtime CPU，统一驱动基座与扩展模型）
```

## 3. 关键数据结构

### DetectedObject（include/Types.h）

```cpp
struct DetectedObject {
    std::string image_path;       // 相对 photo/ 的图片路径
    std::string class_name;       // 当前类别名（可能已被降级改写）
    double x, y, w, h, area;      // 归一化框坐标与面积
    double confidence;            // 检测置信度
    Attr attr;                    // 区域 HSV 均值/标准差、色温、主色名、32 维直方图、LBP 粗糙度、清晰度
    std::string original_class;   // 降级前类别（is_fallback 时非空）
    std::string super_class;      // isa 父类
    bool is_fallback;             // 是否已降级
    int parent_id;                // 父对象 id（>> 产物关联父对象）
    int obj_id;                   // 全局唯一对象 id
    int img_id;                   // 图片 id
    std::map<std::string, std::vector<float>> embeddings;   // embedding_name → 向量（聚类包，V2）
    std::map<std::string, std::string> cluster_ids;         // cluster_name → 聚类 id（V2）
};
```

`ObjectIdGenerator` 为单调递增的全局 id 生成器，由 CacheManager（建缓存）与
ExtensionManager（扩展推理）共享，保证 id 全局唯一。

### ImageAttrs（include/Types.h）

每张图在缓存预处理时计算并随索引持久化：

| 分组 | 字段 |
|------|------|
| 颜色 | `color_temperature` `avg_hue` `avg_saturation` `avg_value` `dominant_color` `global_hue_hist[32]` |
| 曝光 | `luma_hist[64]` `overexposure_score` `underexposure_score` `exposure_goodness` |
| 清晰度 | `global_blur_score` `global_blur_raw` |
| EXIF | `camera_make` `camera_model` `iso` `shutter_speed` `aperture` `focal_length` `datetime_original` `width` `height` |
| 用户标记 | `user_tags`（键值对，GUI 详情面板编辑） |
| 场景 | `scene_vector[365]` `dominant_scene` `indoor_score`（Places365） |
| 聚类 | `cluster_groups`（key=cluster_name → 该图聚类 id 列表，V2） |

### TagFilter（include/Types.h）

```cpp
struct TagFilter {
    std::string key;                 // 标签名
    std::vector<std::string> values; // 允许值（OR）；空 = 该 key 任意值
};
```

### Value（executor/Context.h）

求值结果类型，可表示 `IMAGE_SET / OBJECT_SET / OBJECT / ATTR / HIST_VEC / NUM / BOOL / STRING / NONE`。

## 4. 数据流

### 查询执行（读路径）

```
DSL 文本
  → Lexer（Token 流）
  → Parser（AST）
  → Evaluator
      ├─ $          → ImageSet（全库；标签预筛选激活时仅含匹配图片）
      ├─ $ : (cond) → 逐图求值条件，score>0 保留（FilterExpr）
      ├─ any/all    → 遍历集合、绑定 current_object / current_objects、求值条件
      ├─ %          → 把 ImageSet 中各图对象展开为 ObjectSet
      ├─ cnt(cls)   → 基于 current_objects + isa_map 计数
      ├─ >>         → 调 ExtensionManager.expand（裁剪 + 扩展模型推理）
      ├─ ^          → 从 ObjectSet 提取图片路径去重
      ├─ collection(name) → 相簿 ImageSet（来自缓存索引 `collections` 字段）
      ├─ cluster_id/cluster_sim → 读取聚类 id（V2）
      └─ del        → 调 Context::deleteImagesCallback（→ CacheManager.removeImages）
  → printValue / JSON
```

### 缓存构建（写路径）

```
启动 → ModelRegistry.getActiveBaseModel()
  → CacheManager.loadOrBuildCache()
      ├─ loadIndex()            读 cache/<model>/cache_index.json
      │   └─（旧 metadata.json 自动迁移）
      ├─ applyIncrementalUpdate()   对比 mtime/size，仅推理新增/修改图片，剔除已删除图片
      │   └─ YoloInference.detect(img)（GDI+ 解码 → letterbox → ONNX → 解码）
      │       + SceneInference（Places365）+ exif_reader（EXIF）+ 图像属性计算（直方图/曝光/清晰度）
      │       + embedding 提取（激活的聚类包，V2）
      └─ 若索引缺失/损坏 → buildIndexFromScratch() 全量重建

构建完成后 → CacheManager.runClustering()（对每个 can_cluster 的激活包：
             收集 embedding → DBSCAN → 写回 cluster_ids/cluster_groups → saveIndex）
```

### 标签预筛选（写/读路径）

```
--tag-filter key=v1|v2 （多条件 AND，值 OR）
  → CacheManager.applyTagFilters()
  → Context.setPrefilteredIds()（即使为空集也会标记筛选激活）
  → $ 仅遍历匹配图片（匹配 0 张 → 空结果）
```

### 扩展细化（`>>` 路径）

```
ObjectSet
  → ExtensionManager.expand(parents, ext_name)
      → 过滤 class_name/super_class == pack.parent_class 的对象
      → cropRegion：按对象框 + crop_padding 裁剪并缩放到 input_size
      → loadModel：懒加载扩展 .onnx
      → runClassifier / runDetector（输出 [1,n] 或 [1,4+n,anchors]）
      → 结果坐标映射回原图，生成 DetectedObject（parent_id = 父对象 id）
```

### 聚类管线（V2）

```
CacheManager::inferEntry
  → 对每个匹配 parent_class 的对象 → ExtensionManager.extractEmbedding
      （裁剪 + input_normalize（"imagenet" 逐通道）→ ONNX → L2 归一化 → 存入对象 embeddings[embedding_name]）

CacheManager::runClustering（全量/增量构建后执行）
  → 收集全库该 embedding_name 的 embedding，按 (路径, obj_id) 排序
  → DBSCAN（minPts=1，余弦阈值 cluster_threshold）
  → 聚类 id 格式：<cluster_name>_<parent_class>_<NNN>（如 face_cluster_person_001）
  → 写回对象 cluster_ids[cluster_name] + 重建每图 cluster_groups[cluster_name]
  → saveIndex()
```

## 5. 模型切换与缓存隔离

- `models/registry.json` 的 `active_base` 决定当前模型；
- `CacheManager` 缓存目录 = `cache/<active_base>/`；
- `/reload` 或重启后若 `active_base` 变化，旧缓存被忽略，下次查询自动重建；
- `--base <name>` 可在本次运行覆盖配置文件。

## 6. 扩展点

| 扩展点 | 现状 | 未来方向 |
|--------|------|----------|
| 基座模型 | YOLO 检测器（固定输出 `[1, 4+nc, anchors]`） | 支持 YOLO-World 等开放词汇模型（动态类别输出） |
| 输入尺寸 | 从 meta.json 读取并适配预处理/解码 | — |
| 推理后端 | ONNX Runtime（CPU） | GPU（CUDA）EP |
| 扩展模型输出 | 分类器（`[1,n]`）、检测器（`[1,4+n,anchors]`）、嵌入（`[1,D]`） | 更多格式可扩展 `runClassifier` / `runDetector` |
| 类继承 | `classes.json` 单级/多级继承（子→父→祖父） | — |
| 对象 id | 全局单调计数，跨缓存与扩展共享 | — |

## 7. 构建相关

- 编译标准：C++17；
- 推理后端：ONNX Runtime（`onnxruntime-win-x64-<ver>.zip`，通过 `find_package(onnxruntime)` 接入）；
- 图片解码使用 Windows GDI+ / Windowscodecs（`gdiplus.lib` / `windowscodecs`），替代 OpenCV；
- EXIF 解析为内置实现（`src/utils/exif_reader.{h,cpp}`），无需 exiv2；
- 头文件库 nlohmann/json 通过 CMake 查找（可用 `-DNLOHMANN_INCLUDE_DIR` 指定）。
