# DSL 语言参考

本页是图像检索 DSL 的完整语法与语义参考。每个查询在 REPL 中逐行执行；在执行脚本文件时，
默认打印最后一个表达式的结果（若显式赋值给 `out` 变量则打印 `out`）。

## 0. 推荐语法（新式）

本项目在 4.4 中引入并推荐的新语法：

```
筛选图片：   $ : (条件)          # ImageSet → 保留满足图片级条件的图片
存在判断：   any(条件)           # 当前图片/集合存在满足条件的对象
全称判断：   all(条件)           # 当前图片/集合所有对象都满足
```

```dsl
$ : (any(class == "person"))             # 所有含人的图片
$ : (all(area > 0.05))                   # 所有对象面积都 > 0.05 的图片
$ : (cnt(fruit) > 2 && any(class == "cat"))
```

> 旧量词语法 `$ any (cond)` 仍向后兼容（结果一致），但新代码一律使用新语法。
> `any(...)`/`all(...)` 与旧量词 `any(...)` 在求值语义上等价。

## 1. 数据类型

| 类型 | 说明 |
|------|------|
| `ImageSet` | 图片集合（内部以图片相对路径表示） |
| `ObjectSet` | 对象集合（检测框列表，见下文属性） |
| `Object` | 单个对象 |
| `Num` | 浮点数 |
| `Bool` | 布尔值 |
| `String` | 字符串（双引号 `"..."` 或裸标识符，支持 UTF-8 中文） |
| `HistVec` | 32 维归一化色调直方图（`obj_hist`/`img_hist` 等产生） |

### 对象属性（ObjectSet 中每个对象可访问）

| 属性 | 类型 | 说明 |
|------|------|------|
| `class` | String | 当前类别名 |
| `x` `y` | Num | 中心坐标（归一化 0~1） |
| `w` `h` | Num | 宽 / 高（归一化） |
| `area` | Num | 面积占比（`w * h`） |
| `confidence` | Num | 检测置信度 |
| `super_class` | String | 父类别名（继承映射得到，可能为空） |
| `original_class` | String | 降级前的原始类别（仅在 `is_fallback` 时非空） |
| `attr.h/s/v` | Num | 对象区域 HSV 均值（h 0~360，s/v 0~1） |
| `attr.lbp` | Num | 对象区域 LBP 粗糙度（0~1） |
| `attr.color_temperature` | Num | 区域色温（开尔文） |
| `attr.dominant_color_name` | String | 区域主色名 |
| `attr.local_blur_score` | Num | 对象区域清晰度（0~1，越高越清晰） |

## 2. 运算符

### 算术（优先级高于比较）
`+` `-` `*` `/`

### 比较
`>` `<` `>=` `<=` `==` `!=`

### 逻辑
`&&`（且）`||`（或）`!`（非）

### 集合
`|`（并集）`&`（交集）`-`（差集），作用于 ImageSet / ObjectSet。

### 一元操作符

| 符号 | 作用 | 示例 |
|------|------|------|
| `$` | 全量图库（在量词内部指"当前图片"） | `$` |
| `%` | 提取对象：ImageSet → ObjectSet | `% $ : (any(class == "cat"))` |
| `^` | 上溯图片：ObjectSet → ImageSet（去重） | `^ (% $ : (any(class == "cat")))` |
| `!` | 逻辑非 | `!(area < 0.05)` |

### 筛选 `:` 与条件函数 `any()` / `all()`

```dsl
$ : (any(class == "cat"))                      # 含猫的图片
$ : (all(area > 0.05) && any(class == "dog"))  # 全部对象不小 且 有狗
people = % $ : (any(class == "person"))
```

- `ImageSet : (cond)` → 逐图求值图片级条件 `cond`，保留满足的图片（FilterExpr）。
- `any(cond)` / `all(cond)` → 当前图片/集合范围内，是否存在（全部）满足条件的对象。

### 扩展操作符 `>>`
将 ObjectSet 中匹配扩展包父类的对象，裁剪区域后运行扩展模型，生成新的 ObjectSet
（子对象带 `parent_id` 关联父对象）。

```dsl
parts = flowers >> botany_v1
```

> 扩展包必须出现在 `models/registry.json` 的 `active_extensions` 中，否则报错：
> `Extension "xxx" is not active in registry.`

## 3. 量词 / 条件函数 `any` / `all`

作用在集合上（ImageSet 或 ObjectSet）：

- `ImageSet any (条件)` → ImageSet（保留"存在对象满足条件"的图片）
- `ImageSet all (条件)` → ImageSet（保留"全部对象满足条件"的图片）
- `ObjectSet any (条件)` → Bool（集合内是否存在满足条件的对象）
- `ObjectSet all (条件)` → Bool（集合内是否全部满足条件）

### 完整形式
```dsl
$ : (any(class == "cat"))
$ : (any((class == "dog") && (area > 0.05)))
```

### 简写形式（直接跟类别名，等价于 `class == "类别名"`）
```dsl
$ : (any flower)
% $ : (any flower)
```

## 4. 计数函数 `cnt(类别名)`

统计当前迭代上下文中匹配类别的对象数量（返回 `Num`）：

- 在 `ImageSet : (cond)` 内部：统计**当前图片**内的对象数。
- 在 `ObjectSet any/all` 内部：统计该对象集合内的对象数。
- 顶层使用：统计整个图库。

匹配规则（满足其一即计数）：
1. `obj.class_name == 类别名`
2. `obj.super_class == 类别名`
3. `obj.class_name` 是 `类别名` 的继承子类（依据 `classes.json`）

```dsl
$ : (cnt(fruit) > 2)      # 水果超过 2 个的图片
cnt(human)                # 图库中所有 human（含 person 子类）的总数
```

## 5. 变量与 `out`

- 变量使用 `=` 赋值，**不可重新赋值**（重复赋值报错）。
- 脚本执行完毕后：若存在变量 `out`，打印 `out`；否则打印最后一个表达式的结果。

```dsl
cat_pics = $ : (any(class == "cat"))
dog_pics = $ : (any(class == "dog"))
both = cat_pics & dog_pics
out = both
```

## 6. 删除语句 `del`

`del <目标>` 删除图片文件并同步更新缓存索引（顶层的删除语句）：

```dsl
del "000000000049.jpg"                        # 字符串路径
del $ : (any(class == "cat"))                 # 图片集合表达式
people = % $ : (any(class == "person"))
del people                                    # 变量
```

## 7. 宏系统（Macro）

宏是具名表达式模板，内置宏与用户宏共用同一张宏表，调用语法完全一致。

### 定义用户宏

```
macro <名字>(<参数1>, <参数2>, ...) = <表达式>
```

```dsl
macro half_area(x) = x.area / 2
macro has_big_cat(set) = set : (any(class == "cat" && big))
```

### 调用宏

```dsl
big(obj)              # 显式传参
half_area(obj)        # 用户宏
max(0.1, 0.2)         # 数学函数
warm & bright         # 裸调用：把当前对象广播给单参数宏
```

裸调用（如 `big`、`warm`）在量词内部自动把**当前对象**作为参数传入。

### 内置宏

**数学函数**（返回 Num）：

| 函数 | 说明 |
|------|------|
| `max(a,b)` `min(a,b)` | 最大 / 最小值 |
| `abs(x)` `sqrt(x)` | 绝对值 / 平方根 |
| `pow(a,b)` | 幂 |
| `log(x)` `exp(x)` | 自然对数 / 指数 |

**颜色宏**（基于对象/图像 HSV 与色温）：

| 宏 | 说明 |
|----|------|
| `color(obj, "blue")` | 区域主色是否为指定色（red/orange/yellow/green/cyan/blue/purple/pink/brown/gray/white/black） |
| `cct(obj)` / `warmth(obj)` / `coolness(obj)` / `brightness(obj)` / `saturation(obj)` | 区域色温 / 暖度 / 冷度 / 亮度 / 饱和度（0~1） |
| `img_temp()` / `img_warmth()` / `img_coolness()` / `img_bright()` / `img_colorful()` | 整图色温 / 暖度 / 冷度 / 亮度 / 鲜艳度 |
| `img_color("blue")` | 整图主色是否为指定色 |

**直方图宏**（32 维色调直方图）：

| 宏 | 说明 |
|----|------|
| `obj_hist(obj)` | 对象区域的 32 维直方图 |
| `img_hist()` | 当前图片全局 32 维直方图 |
| `hist_sim(A, B)` | 两个直方图余弦相似度（0~1） |
| `hist_value(obj, idx)` | 对象直方图第 `idx` 个 bin（0~31，越界报错） |
| `img_hist_value(idx)` | 图片全局直方图第 `idx` 个 bin |

> 颜色↔bin 参考：红 0,31 / 橙 1,2 / 黄 3-5 / 绿 9-11 / 青 14,15 / 蓝 19-21 / 紫 24-26 / 粉 27-29。

**图像质量 / EXIF / 用户标记宏**：

| 宏 | 说明 |
|----|------|
| `img_over()` / `img_under()` / `img_exp_good()` | 过曝 / 欠曝 / 曝光质量（0~1） |
| `img_hist_val(idx)` | 亮度直方图 bin（0~63） |
| `img_blur()` / `img_blurry()` / `obj_blur(obj)` | 全局清晰度 / 1-清晰度 / 物体局部清晰度 |
| `img_camera()` / `img_iso()` / `img_shutter()` / `img_aperture()` / `img_fl()` / `img_date()` | EXIF（无则空/-1） |
| `img_tag(key)` / `img_has_tag(key)` / `img_tag_equals(k,v)` | 用户标记查询 |
| `stof(s)` / `str_contains(s, sub)` | 字符串→数值 / 包含判断 |

**空间 / 几何宏**（接收对象，返回 Bool）：

| 宏 | 逻辑 |
|----|------|
| `big(x)` | `x.area > 0.2` |
| `small(x)` | `x.area < 0.05` |
| `left(x)` `right(x)` | 中心 x 在左 / 右 1/3 |
| `top(x)` `bottom(x)` | 中心 y 在上 / 下 1/3 |
| `square(x)` | `abs(x.w/x.h - 1) < 0.1` |

**关系宏**（接收两个对象）：

| 宏 | 逻辑 |
|----|------|
| `left_of(a,b)` | `a.x + a.w < b.x` |
| `above(a,b)` | `a.y + a.h < b.y` |
| `inside(a,b)` | a 的框完全在 b 内 |

**氛围宏**（基于对象区域预处理得到的 HSV 均值与 LBP 粗糙度 `attr`）：

| 宏 | 逻辑 |
|----|------|
| `warm(obj)` | `obj.attr.h ∈ (5, 45)` |
| `cool(obj)` | `obj.attr.h ∈ (180, 260)` |
| `bright(obj)` | `obj.attr.v > 0.75` |
| `dark(obj)` | `obj.attr.v < 0.25` |
| `smooth(obj)` | `obj.attr.lbp < 0.2` |
| `rough(obj)` | `obj.attr.lbp > 0.6` |

### 示例

```dsl
out = $ : (any(warm && bright))                       # 含明亮暖色物体的图片
out = $ : (any(area > half_area(obj)))                # 自定义宏 + 对象广播
out = $ : (any(hist_value(obj, 0) + hist_value(obj, 31) > 0.3))  # 红色占比高
out = $ : (img_exp_good() > 0.9 && any(class == "person"))       # 曝光良好且有人
```

## 8. 运算符优先级（高 → 低）

```
! -（一元） > % ^ $（一元） > .（属性访问） > * / > + - > > < >= <= == != > && > ||
> | & -（集合）> :（筛选）> >> > any/all（后置）
```

## 9. 语法示例汇总

```dsl
# 找包含猫的图片
pic1 = $ : (any(class == "cat"))

# 图片里左边（x<0.3）有猫
pic2 = $ : (any((% $ : (any(class == "cat"))) any (x < 0.3)))

# 含面积>0.2 对象、且该对象不是人的图片
pic3 = $ : (any((% $ : (any(area > 0.2))) any (class != "person")))

# 集合运算：同时包含猫和狗的图片
cat_pics = $ : (any(class == "cat"))
dog_pics = $ : (any(class == "dog"))
result = cat_pics & dog_pics

# 继承计数：水果超过 2 个的图片
fruit_pics = $ : (cnt(fruit) > 2)

# 扩展细化：flower → 花朵部位 → 上溯
flowers = % $ : (any flower)
parts = flowers >> botany_v1
out = ^ parts

# 标签预筛选在命令行完成，DSL 内无需感知：
#   dsl.exe --json --photo .\photo --tag-filter "city=sh|bj"
```

## 10. 备注

- 类别名、属性名均为 UTF-8 编码；脚本文件请保存为 UTF-8（无 BOM）。
- 字符串字面量 `"..."` 与裸标识符在类别名语境中等价。
- 属性比较 `class == "cat"` 中的 `class` 是当前对象的类别；在量词内部迭代时自动绑定当前对象。
- `del` 为不可逆操作：会删除磁盘文件并更新缓存索引。
