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
- 实现状态（2025 E 首批）：`src/truepeak.{c,h}`，登记名 `truepeak 前瞻限幅`（`09_truepeak.wav`，链路 [9]），Makefile SRCS 已登记。系数表含 22 个负系数（从 ITU PDF 逐字录入，pdfminer 提取核对 + broadcast-loudness 交叉验证）；检测链 float 域省略 12.04 dB 预衰减（Annex 2 §3 明示可省）。实测（合成 1 s，`--no-match`）：节目输出 dBTP = −1.0 dB ≤ −0.7，安全钳位 1；门槛 PASS（听感条款免执行）。注：满幅正弦峰在采样点上时 dBTP ≈ 0（滤波器增益归一），方波 1 kHz 实测 +2.0 dB ≈ 4/π 重建峰（理想 sinc +2.10 dB，48 阶 FIR 限带略低）——“满幅正弦 ≈ +3.01 dB”的校验点与标准行为不符，未采用（见 ADR-0003）。

### 2.3 D 软拐点压缩器（Giannoulis/Massberg/Reiss JAES 2012）

- 增益计算机 §2.2 Eq(4)（软拐点抛物线），分支峰值检测 §2.3.3 Eq(15)。
- 默认参数：T = −18 dBFS（对齐仓库响度锚点）、R = 4:1、W = 6 dB、attack 10 ms、release 100 ms。
- **门槛**：增益只衰减不放大；输出样本峰 ≤ 0 dBFS。
- 实现状态（2025 D 首批）：`src/comp_soft.{c,h}`，登记名 `softknee`，Makefile SRCS 已登记。检测器按论文 §2.4.1 Eq(20) 推荐放线性域（dB 变换前）；T 语义为叠加信号 dBFS 域，无检测域缩放。Eq(15) 释放支路为纯放电 `αR·yL[n−1]`（Eq(16) 是带 (1−αR)·xL 项的变体，未用）。实测（合成 1 s）：增益只衰减，最小增益 −11.6 dB，样本峰 −7.2 dBFS ≤ 0，安全钳位 0；门槛 PASS（听感条款免执行）。

### 2.4 A 三次软削波（JOS PASP §9.1.6）

- 三分段公式：`|x|≤1` 时 `f(x)=x−x³/3`；`|x|>1` 时钳到 `±2/3`（曲线端点值）。±1 处斜率为 0（C0 饱和；tanh 实现是 C1，谱形与接点连续性可对比）。
- **带宽扩展因子 = 3** 写入文档；钳位统计语义与 tanh 一致（统计 |x|>1 的样本，返回钳位样本数）。

## 2.5 首批门槛跑数实测（#6，`--sec 2 --no-match`，48 kHz 合成）

| 算法 | 门槛 | 实测 | 判定 |
|---|---|---|---|
| cubic 三次软削波 | 钳位语义与 tanh 一致；带宽=3 入文档 | 钳位 1390（与线性相加同样本集）；文档已载 | PASS |
| softknee 压缩器 | 增益只衰减；样本峰 ≤ 0 dBFS | 最小增益 −12.0 dB 无放大；样本峰 −5.8 dBFS | PASS |
| truepeak 前瞻限幅 | dBTP ≤ −0.7 dB | **−1.0 dB** | PASS |

观察（雾②）：现有合成素材已稳定暴露样本间峰——contra dBTP +2.7 dB、线性相加 +0.1 dB，无需新合成 ISP 素材。首读发现：contra 是全表唯一 dBTP 严重超标的逐样本公式（0 dBFS 峰下藏 +2.7 dB 重建峰），truepeak 链路的生态位被数据证实。

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
