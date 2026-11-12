# 调研：现有五算法之外的混音/限幅候选算法

> 票 #2 research 调研结论。调研对象：仓库现有五算法（线性硬饱和、平均、防削波 `a+b-ab/M`、contra `(a²+b²)/(a+b)`、tanh 软饱和，见 `src/mix_algos.c`）与前瞻限幅器参考链路（`src/limiter.c`）之外，文献中值得收录的**二路混音逐样本公式**与**限幅/动态处理链路**候选。一手来源优先（ITU-R / ITU-T / JAES 教材论文），每条结论附引用。

## 0. 现有生态位盘点（对照基线）

| 现有链路 | 占据的生态位 |
|---|---|
| [1] 线性相加+硬饱和 | 无损基准；过载时硬削波（波形被破坏） |
| [2] 平均 | 纯线性、零失真，固定 -6 dB |
| [3] 防削波 | 同号压缩（有界），异号线性；零点斜率跳变、互调差 |
| [4] contra | 反面教材（反相 / 非单调 / 发散） |
| [6] tanh 软饱和 | 光滑有界软饱和，C1 连续，小信号线性 |
| [5] 前瞻限幅器 | 时间常数动态处理，能区分瞬态与持续响度 |

**未覆盖的轴**（候选要补的位置）：
- **代数多项式族**软饱和（无超越函数、谱可解析）——tanh 占了超越函数族；
- **对数压缩族**（电信 μ/A 律传统）；
- **样本间峰值（true-peak / inter-sample peak）**——现有 limiter 只看采样点，16-bit WAV 输出后 DAC 重建可能过载；
- **软拐点（soft-knee）增益计算机**与**侧链检测器变体**（RMS 检测、分支峰值检测、双时间常数释放、反馈拓扑）——现有 limiter 是 hard-knee + 前馈 + 单释放常数。

---

## 1. 候选清单

### A. 三次多项式软削波（cubic soft clipper）——逐样本公式

**公式**（JOS PASP，Cubic Soft Clipper 页）：

```text
f(x) = -2/3                  x ≤ -1
f(x) = x - x³/3              -1 ≤ x ≤ 1
f(x) = +2/3                  x ≥ 1
```text

工程变体（SoLoud 引擎默认 clipper，一手源码注释）：`f1 = (0.87·f1 - 0.1·f1³)·u1·o1`，其中 `u1=(f1>-1.65)`、`o1=(f1<1.65)`，即同样的三次曲线经平移缩放后接硬钳位（`soloud.cpp`，jarikomppa/soloud `src/core/soloud.cpp` mix 函数 SSE 注释行）。

**精确出处**：
- Julius O. Smith III, *Physical Audio Signal Processing*, W3K Publishing, 2010, ISBN 978-0-9745607-2-4，"Cubic Soft Clipper"（§9.1.6 引用），在线版：<https://ccrma.stanford.edu/~jos/pasp/Cubic_Soft_Clipper.html>；谱分析页 <https://ccrma.stanford.edu/~jos/pasp/Cubic_Soft_Clipper_Spectrum.html>。
- SoLoud Audio Engine 文档 §5.4 Clipping（roundoff clipper 默认启用）与 `src/core/soloud.cpp` 源码（GitHub master）。

**生态位**：与 [6] tanh 同为"光滑有界软饱和"，但换到**代数多项式族**：无超越函数调用（每样本 2 次乘加，最快）；JOS 谱页给出可解析性质——"带宽扩展限制为 3 倍，是奇函数非线性可得的最慢混叠率"，三次谐波为主。与 tanh 构成"代数 vs 超越"的直接 A/B 对照：THD 谱形、接点连续性（此公式在 ±1 处斜率为 0，仅 C0 饱和；tanh 实现是 C1）都有可比差异。

**C99 自研成本**：~20 行，**无状态**。装进统一签名：✅ 直接（ctx 未用）。

### B. 反正切非线性（arctangent）——逐样本公式

**公式**（JOS PASP，Arctangent Nonlinearity 页）：

```text
f(x) = (2/π)·arctan(α·x),  x ∈ [-1,1],  通常 α ≫ 1（示例 α=10, Fig 6.21）
```text

**精确出处**：JOS PASP（同上书）"Arctangent Nonlinearity"：<https://ccrma.stanford.edu/~jos/pasp/Arctangent_Nonlinearity.html>（JOS 强调这是**可逆**（一一映射）无记忆非线性的最简例子）。

**生态位**：全输入域严格单调、光滑、有界，且**可逆**（tanh 软饱和同样可逆，但 JOS 把 arctan 作为可逆非线性的教学基准）；大信号渐近按 1/x 收敛，压缩起音比 tanh 更早更缓。**与现有 [6] 的区分度是所有候选中最低的**（同为超越函数软饱和），收录价值主要在教学对照。

**C99 自研成本**：~15 行，无状态。装进统一签名：✅。

### C. μ 律 / A 律对数压缩（ITU-T G.711 连续压缩律）——逐样本公式

**公式**（ITU 官方软件工具库 STL 手册 g711.tex 给出的连续域定义；G.711 标准本身采用 13/15 折线数字近似）：

```text
μ 律： c(x) = xmax · ln(1 + μ·|x|/xmax) / ln(1+μ) · sgn(x)，μ = 255
A 律： c(x) = A·|x| / (1 + ln A) · sgn(x)                    0 ≤ |x|/xmax ≤ 1/A
       c(x) = xmax·(1 + ln(A·|x|/xmax)) / (1 + ln A) · sgn(x)  1/A ≤ |x|/xmax ≤ 1
       A = 87.6（ITU 选定值；STL 手册正文记 87.56）
```text

大信号 SNR：SNR_μ = 6.02B + 4.77 − 20·log₁₀(ln(1+μ))（μ=255, B=8 时 38.17 dB）。

**精确出处**：ITU-T G.711 (11/1988) *Pulse code modulation (PCM) of voice frequencies* 为标准本体（折线近似）；连续律公式引自 ITU-T STL 手册 *Software Tool Library 2000 User's Manual*，"G.711: The ITU-T 64 kbit/s log-PCM algorithm" 章（`doc/manual/g711.tex`，openitu/STL 仓库 dev 分支）：<https://github.com/openitu/STL/blob/dev/doc/manual/g711.tex>。

**生态位**：**对数域压缩族**，与现有全部幂式/指数式候选不同族；电信文献最深厚（量化 SNR 近似恒定的经典结论）。用作混音输出映射时语义是"线性叠加 s=a+b 后过 μ 律再归一化"——注意小信号段增益 >1（μ=255 时曲线原点斜率 255/ln256≈48.9），会同步放大叠加中的低电平细节与底噪，这在"响度对齐后才测指标"的本仓库框架里是可测、可比的行为。

**C99 自研成本**：~20 行（每样本一次 logf），无状态。装进统一签名：✅。

### D. 软拐点压缩器链路（soft-knee compressor, feedforward）——动态链路

**公式**（Giannoulis/Massberg/Reiss JAES 2012，全部一手原文核对）：

增益计算机（§2.2，hard knee Eq (3)；soft knee Eq (4)）：

```text
yG = xG                                    2(xG - T) < -W
yG = xG + (1/R - 1)·(xG - T + W/2)²/(2W)   |xG - T| ≤ W
yG = T + (xG - T)/R                        2(xG - T) > W
```text

侧链平滑一阶滤波（§2.3 Eq (5)(7)）：`s[n] = α·s[n-1] + (1-α)·r[n]`，`α = e^(-1/(τ·fs))`。
分支峰值检测器（§2.3.3 Eq (15)）：

```text
yL[n] = αA·yL[n-1] + (1-αA)·xL[n]    xL[n] > yL[n-1]
yL[n] = αR·yL[n-1]                   xL[n] ≤ yL[n-1]
```text

增益级（§2.1 Eq (1)）：`ydB[n] = xdB[n] + cdB[n] + M`。

**精确出处**：D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design—A Tutorial and Analysis", *J. Audio Eng. Soc.* 60(6), 2012 June, pp. 399–408，§2.1 Eq (1)、§2.2 Eq (2)–(4)、§2.3 Eq (5)–(9)、§2.3.3 Eq (12)–(15)、§3 Eq (18)–(19)。PDF：<https://joshreiss.github.io/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf>。

**生态位**：现有 `src/limiter.c` 是"窗口峰值 → 目标增益 → 单一平滑"的 hard-knee 前瞻限幅。本候选补两件事：(1) **软拐点**——增益压缩在 W dB 内抛物线渐变进入，阈值附近增益导数连续，听感更柔；(2) **标准压缩器语义**（阈值/比率/拐点宽度三元组），是文献中增益计算机的规范形式，可与现有 limiter 在同一指标框架下公平对照（仓库的慢变增益剥离残差指标正是为动态链路设计的）。

**C99 自研成本**：~60–80 行，**需状态**（侧链滤波器状态 + 增益历史）。装进统一签名：✅ 学 `mix_float_limiter` 把状态藏 ctx（limiter_cfg_t 模式），双音测试复用/覆盖 ctx 的既有约定同样适用。

### E. 真峰值前瞻限幅（true-peak / inter-sample peak limiter）——动态链路

**算法**（ITU-R BS.1770-4 Annex 2 完整规定，系数一手核对）：

处理链：① 12.04 dB 衰减（整数算法余量；float 域可省）→ ② **4× 过采样**（48 kHz→192 kHz）→ ③ 低通滤波 → ④ 取绝对值 → ⑤ dBTP = 20·log₁₀(过采样后峰值) + 12.04。

滤波器为 **48 阶 4 相位 FIR 插值器**，Annex 2 §3 给出完整系数表（相位 0..3 各 12 系数，例如 Phase 2 中央两系数 0.7797851562500 / 0.9721679687500；全表见标准）。欠读界（Appendix 1 to Annex 2）：`maximum under-read (dB) = 20·log(cos(π·fnorm/n))`；表：4× 过采样在 fnorm=0.5 时最大欠读 **0.688 dB**，8× 时 0.169 dB。

**精确出处**：
- ITU-R BS.1770-4 (10/2015) *Algorithms to measure audio programme loudness and true-peak audio level*, Annex 2 §1–3（处理链与 FIR 系数表）、Appendix 1（欠读表）：<https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-4-201510-S!!PDF-E.pdf>。
- EBU R 128-2023 *Audio loudness normalisation & permitted maximum level*，考虑条款 m)：节目真峰值在制作（线性音频）阶段**不得超过 −1 dBTP**，测量容差 ±0.3 dB（带宽 ≤20 kHz）：<https://tech.ebu.ch/docs/r/r128.pdf>。

**生态位**：**唯一针对样本间峰值的候选**。本仓库输出正是 16-bit PCM WAV——DAC 重建时 sinc 插值峰可能超过最大采样值（BS.1770-4 Appendix 1 "peak-sample meter anomalies" 原文详述），现有 limiter 只看采样点、对此盲。把 Annex 2 的 4× FIR 插值接在现有 `limiter_step` 的窗口峰值检测之前，即得 dBTP 可控的限幅链路；EBU −1 dBTP 给出天然的目标上限参数（现有 ceiling 0.8913 ≈ −1 dBFS 正好可以升格为 −1 dBTP 语义）。

**C99 自研成本**：~80–100 行，**需状态**（12 阶 FIR 历史 + 现有 limiter 状态）。装进统一签名：✅ 状态藏 ctx；FIR 系数为静态常量表（48 个 float），无新依赖。

### F. 双时间常数释放（program-dependent / dual release，SSL 式）——动态链路变体

**公式**：两个释放网络堆叠的峰值检测器；Giannoulis §2.3.3 给出数字等价的 decoupled 检测器 Eq (14)：`y1[n] = max(xL[n], αR·y1[n-1])`，`yL[n] = αA·yL[n-1] + (1-αA)·y1[n]`（并指出实际释放时间 ≈ τR+τA）。SSL Stereo Bus Compressor 的双释放设计在 §2.3 描述："短瞬态后释放快、持续压缩后释放慢"。

**精确出处**：Giannoulis et al. 2012 §2.3（Fig 2–4 及正文）、§2.3.3 Eq (13)(14)。

**生态位**：现有 limiter 单一释放常数，长节目里"压完慢慢回"与"瞬态后快速回"不可兼得。生态位真实但**窄**——属于 [5] 的参数化增强而非新链路。

**C99 自研成本**：~40–60 行，需状态。装进统一签名：✅。

### G. RMS 检测器侧链——动态链路变体

**公式**（§2.3.1 Eq (9)）：`y²L[n] = (1/M)·Σₘ x²L[n-m]`（M 点滑动平均，然后开方）。

**精确出处**：Giannoulis et al. 2012 §2.3.1 Eq (9)（原文同时指出 RMS "更接近感知响度，但一般不适合峰值控制"）。

**生态位**：响度感知相关的侧链（vs 峰值检测）。作为压缩链路变体收录价值中等；其滑动平均可递推实现。

**C99 自研成本**：~40 行（递推平方和 + 状态），需状态。装进统一签名：✅。

### H. 反馈型压缩器（feedback topology）——动态链路变体

**公式**（§3 Eq (18)）：`cdB[n] = (1-R)·(yG[n-1] - T)`，xL > T、hard knee、无起释时间。原文明确指出：ratio=∞（限幅）需要无穷大负增益，**反馈型不能做完美限幅**；前馈型 Eq (19) `cdB[n] = (1/R-1)·(xG[n]-T)` 无此问题。

**精确出处**：Giannoulis et al. 2012 §3 Eq (18)(19)。

**生态位**：拓扑维度（前馈 vs 反馈）的历史机型谱系（LA-2A 等反馈设计的数学根源）。作为**对照链路**有教育意义（回应 README"混音 vs 防削波"的教学定位），但作为算法收录价值一般。

**C99 自研成本**：~50 行，需状态。装进统一签名：✅。

---

## 2. 统一签名兼容性总结

统一签名 `long fn(const int16_t*, const int16_t*, float*, int, void *ctx)` 的兼容性极好：**全部 8 个候选都能装进**。

- 无状态逐样本公式（A/B/C）：与现有 [1][2][3][4][6] 完全同构，ctx 空置；
- 有状态链路（D/E/F/G/H）：学 `mix_float_limiter` 的做法——配置+统计外置 `limiter_cfg_t` 式结构、内部状态不对外暴露，双音测试复用并覆盖 ctx 的既有约定照搬即可（注意保持"限幅器统计在双音测试前存入"的顺序约定）；
- 唯一超出逐样本模型的是 E 的 4× FIR 插值，但它只是把"窗口峰值检测"的输入换成过采样流，仍在函数内部完成，签名不变；
- 没有候选需要全局信息或两遍扫描；前瞻/历史窗口都在函数内部分配（现有 limiter 已示范）。

结论：签名不构成任何候选的排除条件；取舍只看生态位与成本。

---

## 3. 排序建议（指标可比性 × 实现成本 × 生态位独特性）

| 排名 | 候选 | 指标可比性 | 实现成本 | 生态位独特性 | 一句话 |
|---|---|---|---|---|---|
| **1** | **E 真峰值限幅** | 高：现有峰值/RMS/波峰因数/残差全适用，且自带新指标 dBTP（附录欠读公式可验证实现） | 中（~100 行，需状态+FIR 表） | **独一无二**：唯一样本间峰值防护；本仓库输出即 16-bit WAV，DAC 重建过载是真实下游场景；EBU −1 dBTP 提供权威目标值 | 把 [5] 的"看采样点"升级为"看重建波形" |
| **2** | **D 软拐点压缩器** | 高：慢变增益剥离残差指标正是为此设计；与 [5] 同框架 A/B | 中低（~60–80 行，需状态） | 高：补"增益渐变进入压缩"维；JAES 教程公式精确到编号，自研无歧义 | 增益计算机的规范形式，[5] 的软化版对照 |
| **3** | **A 三次软削波** | 高：与 [6] tanh 同指标可比（钳位数/残差/互调），谱性质可解析（带宽扩展=3） | **最低**（~20 行，无状态，无超越函数） | 中高：代数族 vs 超越族；奇非线性最低混叠率，性能与谱形双重卖点 | tanh 的代数对照面，成本几乎免费 |
| 4 | C μ 律 | 中：响度对齐框架可测，但小信号增益>1 改变噪声底语义 | 低（~20 行，无状态） | 中高：对数族独一份，电信文献深 | 语义稍牵强，需明确"叠加后过律再归一" |
| 5 | F 双时间释放 | 中：残差指标适用 | 中（~50 行） | 中窄：[5] 的参数化增强 | 价值依赖 [5] 已在场 |
| 6 | G RMS 侧链 | 中 | 中（~40 行） | 中窄：侧链变体 | 原文自己说不适合峰值控制 |
| 7 | H 反馈压缩 | 中 | 中（~50 行） | 中：拓扑谱系教学价值 | 对照意义大于算法意义 |
| 8 | B arctan | 高 | 最低（~15 行） | **最低**：与 [6] 同族同性质 | 仅作教学对照，不建议首批 |

**首批建议**：E、D、A（即上表前 3）。三者分属"新检测维度 / 新增益语义 / 新曲线族"，互相正交，都能在现有指标框架下直接跑分且文献公式精确到条款/公式号，自研零歧义。

---

## 4. 调研浮现的新问题（供后续会话毕业成票，不自行建票）

1. **dBTP 指标列**：若选 E，表格应加真峰值列（BS.1770-4 Annex 2 的 4× FIR + 欠读公式 0.688 dB @4×/fnorm=0.5 可直接复用为验证基准）——与地图 #1 "Not yet specified" 中"新算法是否需要补充指标"预留方向吻合。
2. **测试素材缺口**：满量程单频正弦经 DAC 重建（sinc 插值）产生样本间过载的素材，现有合成素材（多频叠加/扫频）能否稳定暴露样本间峰，需要验证；必要时补一个"高幅单频近满量程"素材票。
3. **μ 律语义裁定**：候选 C 的"叠加后过律再归一化"是否算"混音算法"（vs 量化器特性展示），选票时应先在 grilling 票里裁定。
4. **限幅器族谱归档**：F/G/H 都是 [5]/D 的变体，登记表（地图 Destination 的算法登记表+实现规范）应决定变体是各自成链路还是作为主链路的参数档出现，避免 algos[] 膨胀。

---

## 5. 参考文献（全部一手来源）

1. ITU-R BS.1770-4 (10/2015). *Algorithms to measure audio programme loudness and true-peak audio level*. Annex 2（真峰值 4× 过采样处理链、48 阶 4 相位 FIR 系数表）、Appendix 1 to Annex 2（欠读公式与表）. <https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-4-201510-S!!PDF-E.pdf>
2. EBU R 128-2023. *Audio loudness normalisation & permitted maximum level*. 考虑条款 m)（−1 dBTP 生产上限，±0.3 dB 容差）. <https://tech.ebu.ch/docs/r/r128.pdf>
3. D. Giannoulis, M. Massberg, J. D. Reiss (2012). "Digital Dynamic Range Compressor Design—A Tutorial and Analysis". *J. Audio Eng. Soc.* 60(6), pp. 399–408. §2.1 Eq(1)；§2.2 Eq(2)–(4)；§2.3 Eq(5)–(9)；§2.3.3 Eq(12)–(15)；§3 Eq(18)–(19). <https://joshreiss.github.io/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf>
4. J. O. Smith III (2010). *Physical Audio Signal Processing*. W3K Publishing, ISBN 978-0-9745607-2-4. "Memoryless Nonlinearities" 系列页：Arctangent Nonlinearity、Cubic Soft Clipper（§9.1.6 引用）、Cubic Soft-Clipper Spectrum. <https://ccrma.stanford.edu/~jos/pasp/Cubic_Soft_Clipper.html>
5. ITU-T G.711 (11/1988) *Pulse code modulation (PCM) of voice frequencies*（标准本体）；连续 μ/A 律公式引自 ITU-T Software Tool Library 手册 "G.711: The ITU-T 64 kbit/s log-PCM algorithm" 章（`doc/manual/g711.tex`，含 SNR 公式与 A=87.56/μ=255 取值）. <https://github.com/openitu/STL/blob/dev/doc/manual/g711.tex>
6. SoLoud Audio Engine 文档 §5.4 Clipping（roundoff clipper）及 `src/core/soloud.cpp` 源码（`(0.87·f - 0.1·f³)` 三次 clipper 的 SSE 实现与 ±1.65 界）. <http://soloud-audio.com> / <https://github.com/jarikomppa/soloud>

（调研过程另核对了 EBU R68-2000 对齐电平与 BS.775 下混——前者与本票无关，后者为多路下混且一手 PDF 不可达，均未列为候选。）
