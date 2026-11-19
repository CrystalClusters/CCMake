# CCM 文件规范

## 概述

`.ccm` 文件是一个声明式环境变量配置器，每个字段就是一个环境变量，供 `.cmk` 执行脚本通过 `@{字段名}` 引用。通常用于描述构建环境——包括工具链位置、源码分布、依赖资源路径、构建参数等。

> 注：CCM文件是纯数据的静态配置，未设计逻辑和对外动作能力。

```
     .ccm             .cmk
 Build Context   Build Procedure
    构建环境         构建流程
       |                |
       +---- ccmake ----+
          脚本解释执行器
               |
            操作系统
```

### 设计原则

| 原则 | 说明 |
|------|------|
| 纯声明式 | 只定义数据，不判断或执行动作 |
| 顺序解析 | 从上到下，后定义覆盖先定义 |
| 环境变量 | 每个字段可直接被 `.cmk` 以 `@{key}` 引用 |

---

## 快速入门

最小可用的 `.ccm` 文件：

```
"cc": "gcc";
"source_dir": "./src";
```

在命令行指定：

```bash
ccmake -c project.ccm -f build.cmk
```

在 `.cmk` 中使用：

```
exec(@{cc} -o hello @{source_dir}/main.c);
```

---

## 基本语法

### 键值对

每个字段以键值对形式定义，值以分号 `;` 结束：

```
"键名": <值>;
```

键名使用双引号包裹的字符串，值可以是字符串、数字、布尔值、嵌套对象或数组。

### 字符串值

```
"project": "CrystalCluster";
"compiler": "gcc";
```

### 数字与布尔值

```
"port": 8080;
"debug": true;
"optimize_level": 3;
```

解析时自动识别为对应类型。被 `.cmk` 引用时按需序列化为字符串。

### 注释

**行注释**：以 `//` 开头，到行尾结束。

```
// 这是一行注释
"version": "1.0";   // 行尾注释也可以
```

**块注释**：以 `/*` 开头，`*/` 结尾，可跨多行。

```
/*
 * 多行注释
 * 适合大段说明或临时屏蔽配置块
 */
"version": "1.0";
```

### 语句分隔符

每个键值对定义必须以分号 `;` 结束。

---

## 复合数据类型

### 嵌套对象

使用 `{}` 定义嵌套结构，通过 `.` 点号进行层级寻址：

```
"toolchain": {
    "c_compiler": "gcc";
    "linker": "ld";
    "ar": "ar";
};
```

在 `.cmk` 中引用：

| 寻址表达式 | 对应值 |
|---|---|
| `${toolchain.c_compiler}` | `gcc` |
| `${toolchain.linker}` | `ld` |

### 数组

使用 `[]` 定义数组，元素之间用逗号或空白分隔：

```
"defines": [
    "CC_USE_VULKAN",
    "NOMINMAX"
];
```

尾部逗号允许（便于 diff 友好）。

### 数组访问

直接引用数组字段名返回以空格分隔的拼接字符串，也支持按索引访问：

| 寻址表达式 | 对应值 |
|---|---|
| `${defines}` | `CC_USE_VULKAN NOMINMAX` |
| `${defines[0]}` | `CC_USE_VULKAN` |
| `${defines[1]}` | `NOMINMAX` |

---

## 文件导入

### @include 基本用法

通过 `@include` 导入其他 `.ccm` 文件的内容。导入文件的所有字段会被合并到当前配置中：

```
@include "path/version.ccm";
```

### 路径规则

- 相对路径：相对于当前 `.ccm` 文件所在目录
- 绝对路径：直接定位

### 多文件组合

```
@include "common/base.ccm";
@include "platform/linux.ccm";
```

### 字段覆写与合并

字段按照从上到下的顺序解析，后出现的字段会覆盖先前的同名字段。利用这一特性，可以在 `@include` 之后覆写默认值：

```
// base.ccm 中定义了 "cc": "gcc"
@include "base.ccm";

// 覆写为 clang
"cc": "clang";
```

对于嵌套对象，合并是**浅合并**：仅覆写同名叶子键，未提及的子键保留原值。

```
// base.ccm
"toolchain": {
    "c_compiler": "gcc";
    "cxx_compiler": "g++";
};

// project.ccm
@include "base.ccm";
"toolchain": {
    "c_compiler": "clang";   // 仅覆写此项，cxx_compiler 仍为 "g++"
};
```

---

## 值引用

### .cmk 中的 @{} 引用

在 `.cmk` 执行脚本中，使用 `@{字段名}` 引用 `.ccm` 配置文件中定义的字段。支持 `.` 层级寻址和数组索引。详细用法参见 [CMK 文件规范](CMK文件规范.md)。

### .ccm 内部的 ${ref} 引用

在 `.ccm` 文件内部，`${}` 语法**仅用于内建函数**。如需引用同一配置文件中的其他字段，使用内建函数 `${ref <字段名>}`：

```
"source_dir": "./src";
"headers": ${find ${ref source_dir} *.h};
```

> **注意：** 直接写 `${字段名}`（不通过 `ref`）在 `.ccm` 中无效。`${}` 在 `.ccm` 中只用于内建函数调用。

---

## 内建函数详解

CCMake 在解析 `.ccm` 配置文件时提供了一些内建函数，它们以 `${函数名 参数...}` 的形式使用，在解析阶段被执行并将结果写入字段。

### 函数总览

| 内建函数 | 说明 |
|---|---|
| `${ref <字段名>}` | 引用同一配置文件中的其他字段值 |
| `${find <目录> <选项...>}` | 在指定目录中查找匹配的文件 |

### ${ref} — 字段引用

**语法：**

```
${ref <字段名>}
```

**参数：**

| 参数 | 说明 |
|------|------|
| `<字段名>` | 要引用的字段名，支持 `.` 层级寻址 |

**行为：** 在解析阶段将 `${ref ...}` 替换为对应字段的当前值。

**示例：**

```
"source_dir": "./src";
"compiler": "gcc";

// 引用单个字段
"all_sources": ${find ${ref source_dir} *.c};

// 引用嵌套字段
"toolchain": {
    "c_compiler": "clang";
};
"default_cc": ${ref toolchain.c_compiler};   // 展开为 "clang"
```

### ${find} — 文件发现

**语法：**

```
${find <目录1> [目录2 ...] -include <通配符...> [-exclude <通配符...>]}
```

**参数：**

| 参数 | 说明 |
|------|------|
| `<目录...>` | 要搜索的一个或多个目录路径 |
| `-include <通配符...>` | 包含匹配的文件，支持 `*` `?` 通配符 |
| `-exclude <通配符...>` | 排除匹配的文件，支持 `*` `?` 通配符 |

**行为：** 递归搜索指定目录，返回匹配的文件路径列表（以空格分隔）。

**示例：**

```
// 查找 src 目录下所有 .c 文件
"c_files": ${find ./src -include *.c};

// 多目录查找
"all_code": ${find ./src ./lib -include *.c *.cpp};

// 排除测试文件
"production_code": ${find ./src -include *.c -exclude *_test.* *mock*};
```

---

## 完整示例

### 单文件项目配置

```
// CrystalCluster 项目配置
"project": "CrystalCluster";
"source_dir": "./src";
"include_dir": "./include";

// 工具链
"toolchain": {
    "c_compiler": "gcc";
    "linker": "ld";
};

// 自动收集源文件
"c_files": ${find ./src -include *.c};

// 构建选项
"c_standard": "c17";
"defines": ["CC_USE_VULKAN", "NOMINMAX"];
```

### 多平台项目配置

```
// common.ccm — 通用配置
@include "base/version.ccm";
"project": "CrystalCluster";
"source_dir": "./src";
"includes": ${find ./include -include *.h};

// linux.ccm — Linux 平台
@include "common.ccm";
"toolchain": {
    "c_compiler": "gcc";
    "linker": "ld";
};
"libs": ["m", "pthread", "dl"];

// windows.ccm — Windows 平台
@include "common.ccm";
"toolchain": {
    "c_compiler": "clang";
    "linker": "lld-link";
};
"libs": ["kernel32", "user32"];
```

使用：

```bash
# Linux
ccmake -c config/linux.ccm -f build.cmk

# Windows
ccmake -c config/windows.ccm -f build.cmk
```

---

## 附录

### 与 CMK 的协作关系

| | `.ccm` | `.cmk` |
|---|---|---|
| 角色 | 声明数据（Build Context） | 执行逻辑（Build Procedure） |
| 能力 | 键值定义、嵌套、数组、文件发现、字段引用 | 变量、控制流、函数、外部命令、增量构建 |
| 引用对方 | 不引用 | 通过 `${字段名}` 取值 |
| 文件导入 | `@include "x.ccm"` | `@include "x.ccm"`（导入数据）+ `@import`（导入接口） |

### 常见问题

**Q：如何引用 config.ccm 中定义的字段？**

在 `.cmk` 中使用 `@{字段名}`，在 `.ccm` 内部使用 `${ref 字段名}`。

**Q：字段覆盖的顺序是怎样的？**

从上到下解析。同一个 `.ccm` 文件内，后面的覆盖前面的。`@include` 的文件在当前文件之前解析，所以当前文件可以覆写导入文件的值。