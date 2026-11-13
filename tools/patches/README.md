# libsys4 本地补丁

`libsys4` 是 git 子模块，指向上游 `nunuhara/libsys4`。我们对它做的修改
**不会随父仓库的 push 上传**，所以每个改动都在这里存一份 diff；
`git submodule update` 之后必须重新应用，否则功能会静默退化。

```bash
# 在 xsystem4/ 下执行
git -C subprojects/libsys4 apply tools/patches/libsys4-gsave-v8.patch
ninja -C build
```

检查是否已经应用：

```bash
git -C subprojects/libsys4 apply --check --reverse tools/patches/libsys4-gsave-v8.patch
# 无输出 = 当前工作树已包含该补丁
```

## 现有补丁

| 文件 | 内容 | 退化后的症状 |
|---|---|---|
| `libsys4-gsave-v8.patch` | ① `gsave_validate_value()` 接受泛型 `AIN_ARRAY`(79) 与 `-1`（未设置）；② `gsave_parse()` 支持 **gsave v8**：版本集合加 8、解析时跳过 `group` 前两个 int32（解压后总长 / 0）；③ `gsave_write()` 对 v8 对称回填解压后总长 | 自己写出的 `AFConfig.asd` 被判 `Invalid save file`；PC 原版通关档 `Collection.asd` 被判 `Unsupported save format` / `Invalid save file` |

来源与实测数据见项目根 `README.md`、`TECH_DEBT.md`
以及 `game/全CG周目继承存档/存档位置+说明.txt`。
