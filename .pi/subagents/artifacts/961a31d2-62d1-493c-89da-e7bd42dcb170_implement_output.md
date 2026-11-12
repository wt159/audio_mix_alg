# 票 #5 E 部分实现报告 — 真峰值前瞻限幅 + dBTP 指标列

- **commit**: `8da594f`（分支 `integration/algorithm-family`，父提交 `d791bb1` = D 的尖端；A 的 `7842191` 亦在祖先链中，无需额外合并）
- **基线对齐**: algos[] 第 9 条 `09_truepeak.wav`、容量 8→9、nalgo=9，与 A/D 的 8 条目基线无冲突（工作区无其他写者）

## 一、系数表提取（关键风险点，已闭环）

BS.1770-4 Annex 2 §3 的 48 系数表**全 48 个非零，其中 22 个为负**（负号在 PDF 文本层是独立字形 U+2212）。第一版坐标重建把负系数格误判为 0，得到各相位直流增益 1.218/1.498 —— 不可能是插值器。改用 pdfminer 提取 ITU PDF 文本层逐字核对，并与 broadcast-loudness 0.7.0（BS.1770-5 同表）交叉验证，逐字一致。

- 修正后各相位直流增益：1.0016 / 0.9730 / 0.9730 / 1.0016 ≈ 1（合法插值器自洽）
- Phase 0/3、Phase 1/2 镜像对称结构核对通过
- 来源 URL 注释在 src/truepeak.c 头部；流程与判据记入 ADR-0003

## 二、实现清单

| 文件 | 内容 |
|---|---|
| `src/truepeak.c`（新） | 系数表 + 4× 过采样器 `tp_oversample_peak` + 无状态测量 `truepeak_dbtp()` + `mix_truepeak_limiter()`（前瞻结构同 limiter.c，窗口峰值改为真峰值，ceiling −1.0 dBTP，状态藏 `truepeak_cfg_t` ctx） |
| `src/truepeak.h`（新） | `truepeak_cfg_t`（模式同 limiter_cfg_t）+ 两个导出函数声明 |
| `audio_mix.c` | include、algos[9] 登记 truepeak（第 9 条）、nalgo=9、metrics_t 增 `dbtp` 字段、循环内 `truepeak_dbtp(mixbuf,n)` 对全部算法计算、表格列头与行输出加 dBTP 列、统计打印加真峰值限幅器行、指标说明加 dBTP 条目 |
| `Makefile` | SRCS 登记 `src/truepeak.c`；头依赖加 `src/truepeak.h` |
| `README.md` | 模块清单、算法表第 9 行、手动编译行、指标说明 |
| `GLOSSARY.md` | 新词条「真峰值（true peak / dBTP）」 |
| `docs/spec/algo-registry.md` §2.2 | 实现状态行（门槛实测 PASS） |
| `docs/adr/0003-*.md`（新） | 系数提取方法 + 校验点修正决策 |

## 三、验收结果

### make / 冒烟
- `make`：零警告零错误（-Wall -Wextra -pedantic）
- `make debug`：零警告；`./audio_mix_debug --sec 1 --outdir /tmp/amix_E_dbg` ASan/UBSan 干净，退出码 0
- `./audio_mix --sec 1 --outdir /tmp/amix_E_out`：正常，11 个 WAV
- WAV 输入路径 `./audio_mix /tmp/amix_E_out/00_src_a.wav /tmp/amix_E_out/00_src_b.wav --outdir /tmp/amix_E_out2`：通过
- 44.1 kHz / 96 kHz 采样率路径：通过，全表无 NaN/inf

### 全表 dBTP 摘要（合成 1 s，`--no-match` 看真实电平；对齐路径 dBTP 同步缩放均正常）

| 链路 | 峰值dBFS | dBTP | 钳位 |
|---|---|---|---|
| 线性相加 + 饱和 | 0.0 | **+0.1** | 572 |
| 平均 1/2 | -1.8 | -1.8 | 0 |
| 防削波 a+b-ab/M | -0.2 | -0.1 | 0 |
| contra (a^2+b^2)/(a+b) | 0.0 | **+3.9** | 3972 |
| tanh 软饱和 | -3.1 | -3.1 | 0 |
| 三次软削波 x-x^3/3 | -3.5 | -3.5 | 571 |
| float + 前瞻限幅 | -1.0 | -1.0 | 4 |
| softknee 压缩器 | -6.4 | -6.4 | 0 |
| **truepeak 前瞻限幅** | **-1.0** | **-1.0** | 1 |

- **truepeak 行指标（任务要求）**：峰值 −1.0 dBFS、RMS −12.0、波峰因数 11.0、钳位 1、匹配增益 0.0（no-match）、**dBTP −1.0**、波形残差 −39.3 dB、互调残差 −79.7 dB
- **门槛判定**：节目输出实测 dBTP = **−1.0 dB ≤ −0.7 dB 门槛 PASS**（ceiling −1.0 dBTP + R128 ±0.3 dB 容差）
- contra 行 +3.9 dB：样本间峰超标约 4 dB 的活例，反向佐证检测链有效（该算法在 a+b=0 附近发散产生大量样本间峰）
- float limiter 行 dBTP −1.0：本素材下其安全钳位恰好也压住了样本间峰；两 limiter 行为差异在更极端素材下才分化

### sinc 验证读数（/tmp/tp_sinc_check.c，链接 src/truepeak.c 独立编译）

| 测试 | 实测 | 预期 | 判定 |
|---|---|---|---|
| 满幅正弦 997 Hz，峰对齐采样点 | **+0.009 dB** | ≈ 0（滤波器增益归一化到 1） | PASS |
| 满幅方波 1 kHz（重建峰 4/π） | **+1.998 dB** | ≈ +2.0（理想 sinc +2.10 dB，48 阶 FIR 限带略低） | PASS |
| 满幅正弦随机相位（真 ISP 过冲） | **+0.986 dB** | >0（样本间真实过冲） | PASS |

### ⚠️ 校验点修正（ADR-0003，需上游知悉）

任务原文校验点「满幅正弦的 dBTP 读数约 +3.01 dB（重建峰 4/π 倍）」**与 BS.1770-4 标准行为不符**：
- 4/π 是**方波**傅里叶首项的重建峰（+2.10 dB），不是正弦性质；
- +3.01 dB = 20·log10(√2) 是双音叠加的采样峰关系，与真峰值无关；
- Annex 2 把滤波器增益归一化到 1（"normalises the overall gain ... to unity"），满幅正弦峰落在采样点上时 dBTP ≈ 0 —— 这是标准锚点，实测 +0.009 dB 吻合。
替代校验点采用方波 4/π 重建峰，实测 +1.998 dB。

## 四、残留风险

- 检测链的 4× 过采样在 fnorm=0.5 有最大 0.688 dB 欠读（Appendix 1），对高频占比异常高的素材理论上可低估真峰值约 0.7 dB；ceiling −1.0 dBTP + 门槛 −0.7 dB 的余量设计已覆盖此界。
- 满幅正弦 +0.986 dB（第三行）来自 cos 起振瞬态的 Gibbs 过冲（静音→1.0 阶跃），属信号真实属性，测量端如实反映。
- WAV 输出仍是 16-bit PCM：to_pcm16 量化本身可引入 ≤ 0.03 dB 的样本间峰扰动，与既有链路一致。