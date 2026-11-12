# 票 #5 A 部分实现报告：三次软削波（cubic soft clipper）

- **Commit**：`7842191`（`audio_mix: 新增三次软削波 x-x³/3 软饱和（票 #5 A 部分）`，分支 `integration/algorithm-family`）
- **基线确认**：worktree HEAD 起点即 `integration/algorithm-family`（15a10dd），`git merge-base --is-ancestor integration/algorithm-family HEAD` 通过；实现完成后再次 merge 已 "Already up to date"（提交直接落在本分支，无需合并）。
- **规范依据**：`docs/spec/algo-registry.md` §2.4（JOS PASP §9.1.6）；公式已对照一手来源逐字核对（ccrma.stanford.edu/~jos/pasp/Cubic_Soft_Clipper.html，含 `f(x)=x−x³/3`、±2/3 端点钳位）。

## 实现内容

- `src/mix_algos.c`：新增 `mix_audio_cubic_soft`（链路 [7]），三分段公式
  `y = −2/3 (x<−1)`、`y = x−x³/3 (|x|≤1)`、`y = +2/3 (x>1)`；统一签名
  `long fn(const int16_t*, const int16_t*, float*, int, void*)`，无状态（ctx 未用），
  返回 |x|>1 进入平台段的钳位样本数（语义与 tanh 一致）。注释含：奇对称/单调性、
  C0 接点（±1 处斜率 0，与 tanh 的 C1 对照）、带宽扩展因子=3、|y|≤2/3 有界。
- `src/mix_algos.h`：声明同步。
- `audio_mix.c`：`algos[]` 登记为 [7]（名称「三次软削波 x-x^3/3」，文件 `06_cubic_soft.wav`，参考链路 float 顺延为 [8]/`07_float_limiter.wav`），`nalgo=7`，数组容量 6→7，文件头算法清单同步。
- `README.md`：算法表加第 7 行（带宽扩展=3 写入文档，§2.4 要求）、「五个→六个逐样本公式」。
- `GLOSSARY.md`：新术语「软削波」「带宽扩展因子」两条。
- 未触碰 D/E 文件区域（`src/comp_soft.*`、`src/truepeak.h`、Makefile、音频文件名区域以外无改动）。

## TDD（红绿以构建+冒烟+独立 oracle 代替）

本仓库无测试框架，替代流程：

1. **红**：先写独立单元验证 `/tmp/amix_A_test/test_cubic.c`（自带 oracle 参考实现，16 项断言：零点、±1 接点值与不计数、平台段、奇对称、全域 65536 点逐点对比+单调+有界+计数、无状态重复调用），对未实现函数编译报 undefined reference → 红。
2. **绿**：实现后全部通过（**16/16 ok**）。中途 1 项失败是测试自身边界容差用了 1e-9 而输出为 float32（最近邻 2/3 差约 2e-8），实现逐点匹配 oracle（tol=0）——修正测试容差后通过，实现无改动。
3. 临时测试脚手架按约定不入库。

## 验收结果（对 commit `7842191` 的干净 checkout 运行）

| 项目 | 结果 |
|---|---|
| `make`（-O2 -Wall -Wextra -pedantic） | **零警告零错误** |
| `make debug`（ASan/UBSan） | 干净构建，运行无 sanitizer 报告 |
| `./audio_mix --sec 1 --outdir /tmp/amix_A_out` | 退出码 0，表格含 cubic 行 |
| `./audio_mix_debug --sec 1 --outdir /tmp/amix_A_dbg` | 干净（无 ASan/UBSan 输出，退出码 0） |
| `./audio_mix src_a.wav src_b.wav --outdir ...`（WAV 输入路径） | 通过，`06_cubic_soft.wav` 正常生成 |
| 指标 NaN/inf | 无（表内唯一 "inf" 是说明文字「-inf 表示完全线性」） |
| float 域 \|peak\| ≤ 1.0 | 满足：--no-match 实测峰值 −3.5 dBFS = 2/3 上限 |

### 指标表中 cubic 行（--sec 1，两处独立运行一致）

```
三次软削波 x-x^3/3        -11.5   -18.0      6.5      571     -7.9       -19.1       -10.0
```

即：峰值 −11.5 dBFS、RMS −18.0 dBFS、波峰因数 6.5、**钳位 571**、匹配增益 −7.9 dB、
波形残差 −19.1 dB、互调残差 −10.0 dB。对照 tanh（同段）：−9.5/−18.0/8.5/0 钳位/−10.7/−14.3——
cubic 互调残差略高于 tanh（−10.0 vs −14.3），符合 C0 接点/代数族的预期差异方向。

### 钳位统计

- 节目素材（--sec 1，合成 1 秒）：**571 个样本** |x|>1 进入平台段
  （对照线性硬饱和 572、contra 3972；tanh 0）。
- 全域单元验证：65536 个输入点中 |x|>1 者 32767 个，函数返回值精确等于 32767。
- 单元断言：x=±1 恰在接点（属中段）不计数；x>1/x<−1 各计 1。

## 独立验证快照

由于实现/验收期间另一实现者（D/E，softknee 压缩器 + truepeak）向同一 worktree 并发写入
（audio_mix.c 出现其未完成 hunk，共用工作区一度编译失败——错误全部位于其 `comp` 引用，
与 A 无关），验收对 **commit 树单独 checkout** 重跑：构建零警告 + debug 干净 + 冒烟 + WAV
路径全部通过，指标与共用工作区完全一致。提交采用 hunk 级拆分（`git add` 指定文件 +
A-only audio_mix.c 临时版本），D/E 的未暂存改动全部保留在工作区未动。该情况已向
supervisor 发 progress_update。

## 遗留

- 无阻塞项。pi-lens 的 2 条 `_WIN32` 保留标识符提示为存量代码（`#ifdef _WIN32` 平台分支），非本次引入，未动。
- 听感条款按规范 §3 首批免执行（未执行 A-B 盲听）。
- dBTP 指标列（§4）属 E 部分，未在本次范围内。