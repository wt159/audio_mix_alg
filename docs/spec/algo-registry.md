# 算法登记表与实现规范

> 状态：随首批（E/D/A）实现推进填充。来源：[地图 #1](https://github.com/wt159/audio_mix_alg/issues/1) · [调研 #2](https://github.com/wt159/audio_mix_alg/issues/2) · [选票 #3](https://github.com/wt159/audio_mix_alg/issues/3) · [规范票 #4](https://github.com/wt159/audio_mix_alg/issues/4)（均已关闭）。
> 调研全文：`research/mix-algo-survey` 分支 `docs/research/mix-algo-survey.md`（203 行，一手引用）。

## 1. 登记表结构

每算法一节，字段固定：

- **名称**：登记名（CLI 表格与 WAV 文件名使用）
- **类型**：逐样本公式 / 动态链路
- **公式出处**：精确到条款/公式号的一手来源
- **生态位**：一句话——相对现有五算法补了什么
- **ctx 约定**：无状态=空置；有状态=结构名 + 统计字段 + 双音测试复用语义（统计须在双音测试**前**存入）
- **参数默认值**：全部可调参数及默认值
- **指标实测**：resid_db / resid_shape / 钳位与增益统计 / 峰值 / RMS / 波峰因数 / **dBTP**
- **听感条款**：A-B 盲听 vs 线性硬饱和基准，判据"不劣于"（首批免执行，此列占位）
- **验收状态**：PASS / FAIL / 免执行

## 2. 指标门槛数值判据

### 2.1 通用（全部新算法）

- `make` 零警告（`-Wall -Wextra -pedantic`）。
- debug 构建（ASan/UBSan）干净跑完 `--sec 1` 与 WAV 输入路径冒烟。
- 指标行无 NaN/inf；`-inf` 残差 = 完全线性，属正常。
- float 域输出 `|peak| ≤ 1.0`。

### 2.2 E 真峰值前瞻限幅（ITU-R BS.1770-4 Annex 2 + EBU R128 §m）

- 检测链：4× 过采样、48 阶 4 相位 FIR，系数表以 BS.1770-4 Annex 2 逐字核对。
- ceiling = −1.0 dBTP；**门槛 = 节目输出实测 dBTP ≤ −0.7 dB**（−1.0 上限 + R128 ±0.3 dB 容差）。
- 实现复用检测 FIR 为指标列：对全部算法输出计算 dBTP（新增指标列，见 §4）。

### 2.3 D 软拐点压缩器（Giannoulis/Massberg/Reiss JAES 2012）

- 增益计算机 §2.2 Eq(4)（软拐点抛物线），分支峰值检测 §2.3.3 Eq(15)。
- 默认参数：T = −18 dBFS（对齐仓库响度锚点）、R = 4:1、W = 6 dB、attack 10 ms、release 100 ms。
- **门槛**：增益只衰减不放大；输出样本峰 ≤ 0 dBFS。

### 2.4 A 三次软削波（JOS PASP §9.1.6）

- 三分段公式：`|x|≤1` 时 `f(x)=x−x³/3`；`|x|>1` 时钳到 `±2/3`（曲线端点值）。±1 处斜率为 0（C0 饱和；tanh 实现是 C1，谱形与接点连续性可对比）。
- **带宽扩展因子 = 3** 写入文档；钳位统计语义与 tanh 一致（统计 |x|>1 的样本，返回钳位样本数）。

## 3. 听感门槛条款（首批免执行）

- 流程：A-B 盲听，基准 = 线性硬饱和算法输出（`--no-match` 真实电平）。
- 判据：新算法不劣于基准（人声/音乐节目素材各一段）。
- 首批状态列填"免执行"；执行另起 effort。

## 4. dBTP 指标列

- 指标表新增 dBTP 列，对**全部**算法（含现有五算法与参考链路）的节目输出计算。
- 计算复用 E 的 4× FIR 检测链（BS.1770-4 Annex 2 规定的过采样峰值 + 20·log10 取 dBTP）。
- 验证基准：欠读公式 0.688 dB @4×/fnorm=0.5（BS.1770-4 Appendix 1 to Annex 2）。

## 5. 验收标准（每个新算法）

1. `src/mix_algos.c`（或新模块，新 `.c` 登记 Makefile `SRCS`）+ 头文件声明。
2. `main()` `algos[]` 登记 + `nalgo` 与数组容量同步。
3. README 算法表同步。
4. §2.1 通用门槛 + 本算法专属门槛全部 PASS。
5. 新术语进 `GLOSSARY.md`（仅领域概念，不含实现细节）。
6. 难逆且费解的决策记 ADR（`docs/adr/`，编号递增）。
