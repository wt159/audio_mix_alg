# D 软拐点压缩器 — 实现报告

## 提交

- **commit**：`d791bb1`（在 `integration/algorithm-family` 上，父提交 `7842191` 为 A 的三次软削波）
- **merge 步骤**：本 worktree 即 integration 分支，我的提交本身就是新尖端；显式执行 `git merge integration/algorithm-family` 返回 `Already up to date`。A 的改动完整保留（同分支先行提交）。

## 文件

| 文件 | 内容 |
|---|---|
| `src/comp_soft.c`（新，148 行） | 增益计算机 Eq(4) + 分支峰值检测 Eq(15) + α 系数 Eq(7)；状态 `comp_soft_t` 藏于函数内，cfg 结构外置 |
| `src/comp_soft.h`（新） | `comp_soft_cfg_t`（参数 + 统计输出字段，学 `limiter_cfg_t`）+ `mix_soft_knee` 统一签名 |
| `Makefile` | SRCS 登记 `src/comp_soft.c`；头依赖行加 `comp_soft.h`；补 2 行 shellcheck 误报消音注释 |
| `audio_mix.c` | algos[7] = "softknee 压缩器"（08_softknee.wav）；数组容量 7→8、nalgo=8；压缩器统计双音测试前存入；表格尾新增压缩器统计行 |
| `README.md` / `GLOSSARY.md` / `docs/spec/algo-registry.md` §2.3 / `docs/adr/0002` | 算法表、软拐点/分支峰值检测器术语、实测值落盘、检测器域决策 ADR |

## 公式实现（一手核对 JAES 2012 原文 PDF）

- **Eq(4)** 软拐点三分支：`yG=xG`（`2(xG−T)<−W`）/ 抛物线（`|xG−T|≤W/2`）/ `T+(xG−T)/R`（`2(xG−T)>W`）。数值验证：全域 `yG≤xG`、拐点两侧连续（−2.2499↔−2.2501）、过拐点 yG 斜率 = 1/R = 0.25、`yG(+6 dBFS) = −12 dB` 与解析式精确一致、W=0 退化为硬拐点。
- **Eq(15)** 分支峰值检测器：升 `yL=αA·yL+(1−αA)·xL`，降 `yL=αR·yL`（纯放电）。**关键修正**：调研笔记 Eq(15) 行漏了纯放电这一特征（释放支路无 `(1−αR)xL` 项——那是 Eq(16) 变体），实现按原文 Eq(15)。
- **α = exp(−1/(τ·fs))**（Eq(5)(7)）；检测器放**线性域、dB 变换之前**（论文 §2.4.1 Eq(20) 推荐位置），`xG = 20·log10(yL)`，`cdB = yG − xG`。
- 无检测域缩放：T=−18 dBFS 是叠加信号 dBFS 字面语义（曾实现 10^((T+W/2)/20) 检测放大后主动删除——那会把 T 重映射到 −3 dBFS 输入域，与规范冲突）。
- 无前瞻（压缩器语义：起控平滑即设计目标），瞬态残余由安全钳位兜底。

## softknee 行指标（release，`--sec 1` 合成素材，48 kHz）

```
softknee 压缩器   峰值 -7.2 dBFS   RMS -18.0 dBFS   波峰因数 10.8   钳位 0
                 匹配增益 -0.7 dB   波形残差 -34.0 dB   互调残差 -13.7 dB
压缩器: 平均增益 -6.8 dB, 最小增益 -11.6 dB（最大衰减 11.6 dB）, 安全钳位 0,
        T=-18 dBFS, R=4:1, W=6 dB, 起控 10 ms, 释放 100 ms
```

`--no-match`（真实电平，2 s）：样本峰 −5.8 dBFS ≤ 0 dBFS，最小增益 −12.0 dB。

## 增益统计（门槛判据）

- **只衰减不放大**：✅（两层保障——Eq(4) 各分支解析上 `yG≤xG`，样本环内再对 `cdB>0` 硬钳 0）
- **最大衰减量**：11.6 dB（合成 1 s 素材最小增益 −11.6 dB；`--no-match` 下 −12.0 dB）
- 输出样本峰 −7.2 dBFS ≤ 0 dBFS ✅；安全钳位（float 越界兜底）0 次

## 验收摘要

| 项 | 结果 |
|---|---|
| `make` 零警告（−O2 −Wall −Wextra −pedantic） | ✅ 0 warning/error |
| debug ASan/UBSan `--sec 1` | ✅ 干净退出（stderr 空） |
| debug WAV 输入路径 | ✅ RC=0 |
| release 表格含 softknee 行 | ✅ 8 行算法表 |
| 无 NaN/inf（`-inf` 残差语义除外，本行无 −inf） | ✅ |
| WAV 输入路径（release） | ✅ RC=0 |
| Eq(4) 静态特性数值验证（连续/斜率/极限/W=0 退化） | ✅ 全过 |
| 规范 §2.3 门槛（只衰减、峰 ≤ 0 dBFS） | ✅ |

## 并发协调记录

- 运行中途 A 的实现以未提交形式出现在共享 worktree，随后（12:21:09）A 自行提交为 `7842191`；我的登记从槽位 [6] 顺延至 [7]（WAV 08_softknee.wav），双方改动均保留。
- 提交时 E 的 `src/truepeak.{c,h}` 已出现在工作区（未提交、未登记）——**未纳入我的提交**，留 E 完成。

## 遗留风险

- 释放支路纯放电（Eq(15)）在静音期把包络放尽到 0，检测电平钳 −180 dB 下限保护 log10；对增益计算机无影响（低于拐点即单位增益），但意味着极长静音后第一个瞬态的检测历史为空（与限幅器行为一致，非缺陷）。
- ratio ≤ 0 的畸形配置会除零（limiter_cfg_t 同等暴露面，主流程写死 4:1，未加防御）。
- 首批无自动化测试（仓库无测试设施），红绿以构建+指标冒烟代替，另附独立数值验证程序验证 Eq(4)（见上文，未入库）。