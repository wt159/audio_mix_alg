# G.711 对数律事实清单：立法本义、脱离量化器先例与数学性质

> 产出：GitHub issue #9（wayfinder:research，地图 #8「对数律语义裁定：μ 律与 A 律」子票）调研结论文档。
> 调研日期：2026-11-12。一手来源优先：ITU-T G.711 (11/1988) 原文、ITU-T STL-2000 手册 `g711.tex`、BSTJ 经典论文、芯片数据手册、DAFX/PASP 文献。
> 每条事实标注证据类型：**[直接]** 原文引文；**[解读]** 对原文的解释；**[推导]** 由公式直接推导（本文核算）；**[推断]** 研究者推断（未见原文直陈）。

## 0. 结论摘要

G.711 的立法载体是 8-bit 判决值表（折线），连续 μ/A 律公式只是 ITU 官方手册给出的数学定义（g711.tex 明言「标准不规定上述形式的律」）；脱离量化器把连续对数律当无记忆波形非线性的先例存在于电信域（OFDM PAPR 压扩），音频域软削波文献的曲线族（tanh/arctan/多项式）中未发现 μ/A 律先例；数学上两条连续曲线严格单调、有界、原点小信号线性（μ 律一阶斜率 46.0、A 律严格线性段斜率 16.0），形式上满足「原点线性 + 单调有界」判据，但**不存在任何阈下单位增益段**——在 (0,1) 内全程增益 > 1。另发现既有调研文档两处需更正：μ/ln(1+μ) ≈ 46.0（非 48.9）、Holbrook & Dixon 年份为 1939（非 1949）。

---

## 1. 立法本义（μ 律/A 律为何而设）

**1.1 G.711 标准本体以判决值表立法，正文不含连续公式。**
ITU-T G.711 (11/1988) §3.2："Two encoding laws are recommended and these are commonly referred to as the A-law and the μ-law. The definition of these laws is given in Tables 1a/G.711 and 1b/G.711 and Tables 2a/G.711 and 2b/G.711 respectively."；§3.3："The number of quantized values results from the encoding law."；§3.1 规定每样本 8 bit；§3.6 规定与 13 bit（A 律）/14 bit（μ 律）均匀 PCM 的映射表。
**[直接]** 出处：ITU-T Rec. G.711 (11/1988), *Pulse code modulation (PCM) of voice frequencies*, https://www.itu.int/rec/dologin_pub.asp?id=T-REC-G.711-198811-I%21%21PDF-E&lang=e

**1.2 参数数值 μ=255、A=87.6/87.56 不出现在 G.711 正文文本中。**
在提取到的 G.711 (11/1988) 全部正文中未见 μ=255 或 A=87.6/87.56 字样；数值仅隐含于判决值表。**[直接]**（基于 PDF 文本提取；表格页为数字矩阵）同上出处。

**1.3 连续律公式是 ITU-STL 官方手册给出的数学定义，且手册明言标准不采用它。**
ITU-T STL-2000 手册 g711.tex 给出：
μ 律 `c(x) = xmax · ln(1 + μ|x|/xmax)/ln(1+μ) · sgn(x)`；
A 律两段式 `A|x|/(1+lnA)·sgn(x)`（0 ≤ |x|/xmax ≤ 1/A）与 `xmax·(1 + ln(A|x|/xmax))/(1+lnA)·sgn(x)`（1/A ≤ |x|/xmax ≤ 1）。
并明确："The G.711 standard does not specify the law as defined above, but rather uses a good linear-piecewise approximation for 8 bit samples, which has easier implementation (in hardware)"。折线为 μ 律 15 段、A 律 13 段；A 律线性输入范围 ±4096（13 bit）、μ 律 ±8159（14 bit）；动态范围意义上 A 律等效 12 bit、μ 律等效 13 bit 线性量化。
**[直接]** 出处：ITU-T STL-2000 手册，"G.711: The ITU-T 64 kbit/s log-PCM algorithm" 章，`doc/manual/g711.tex`，openitu/STL 仓库 dev 分支，https://github.com/openitu/STL/blob/dev/doc/manual/g711.tex

**1.4 设计动机：均衡量化 SNR 随幅度的变化。**
g711.tex："If one thinks of using the most natural quantization scheme, one will choose linear quantization. But one drawback of this approach is that the signal-to-noise ratio (SNR) varies with the amplitude of the input signals: the smaller the amplitude, the smaller the SNR... To avoid this problem, one can use logarithmic quantization, which will result into a more uniform quantization noise." 并给出大信号 SNR 公式：SNR_μ = 6.02B + 4.77 − 20·log₁₀(ln(1+μ))，μ=255、B=8 时 = 38.17 dB；SNR_A = 6.02B − 10.1 = 38.06 dB；手册正文记 "The ITU chose the values A=87.56 and μ=255"。**[直接]** 同上出处。

**1.5 μ 律的学术源头是 Smith 1957 的「瞬时压扩 = 等效非均匀量化」理论。**
B. Smith, "Instantaneous Companding of Quantized Signals", BSTJ 36(3), 1957（起页 653）。摘要首句："Instantaneous companding may be used to improve the quantized approximation of a signal by producing effectively nonuniform quantization."；目的："The present discussion is devoted to the formulation of general quantitative criteria for the choice of a suitable companding characteristic."；μ 律公式（式 8）：v = (V/log(1+μ))·log(1+μe/V)；结论："the calculated companding improvement for the weakest signals proves to be equivalent to the addition of about 4 to 6 digits per code group"。论文以恒定 D（RMS 误差/信号比）为最优准则，在典型话音音量分布下最优 μ 达千级（≈2500）量级。**[直接]**（OCR 残缺处按上下文重建已标注）出处：https://archive.org/details/bstj36-3-653（全文 https://archive.org/download/bstj36-3-653/bstj36-3-653_djvu.txt）

**1.6 压扩器（compandor）是另一条独立谱系：带时间常数的增益控制，不是瞬时对数曲线。**
ITU-T Rec. G.162（电话压扩器特性）：压缩比 α、扩张比 β 首选值均为 2（不得超 2.5），电平范围压缩器输入 +5 ~ −45 dBm0；"The permissible limits shall be not greater than: 5 ms for attack, 22.5 ms for recovery"；用途为降噪（"provided sufficient noise improvement is obtained"），并权衡压缩器引入的平均功率增加（0 dBm0 不受影响电平时约 2~3 dB）。**[直接]** 出处：https://www.itu.int/rec/dologin_pub.asp?id=T-REC-G.162-198811-W%21%21PDF-E&lang=e
芯片例证：Motorola/NXP MC33111 compander，"Each circuit has a full wave rectifier to provide average value information to a variable gain cell"；60 dB 动态范围压到 30 dB 再扩张回 60 dB；"Vo(dB) = 0.5 x Vi(dB)"（2:1）；attack 3.0 ms / decay 14 ms（typ，1.0 μF）；**手册全文无 μ-law、无 G.711、无 μ=255 字样**。**[直接]** 出处：https://www.nxp.com/docs/en/data-sheet/MC33111.pdf

**1.7 ⚠ 年份更正：Holbrook & Dixon 是 1939 年，非 1949 年。**
票 #9 正文所引「Holbrook & Dixon 1949 BSTJ Load rating theory」实际为 BSTJ 18(4), October **1939**（archive.org 条目标题与 IEEE Xplore 元数据一致）。该文提供多路放大器负载额定/话音音量统计，是 FDM 载波系统与压扩器设计的音量统计经典；它与 Smith 1957 μ 律推导之间的直接引用链未在本次 OCR 文本中逐条核实。**[直接]**（卷期年份）出处：http://archive.org/details/bstj18-4-624 ；https://ieeexplore.ieee.org/document/5058129

---

## 2. 脱离量化器的先例（连续对数律用作波形域非线性）

**2.1 最强先例：OFDM PAPR 压扩变换——无量化器、逐样本作用于波形。**
μ 律压扩变换在 OFDM 文献中被直接施加于发射波形以降低峰均比，不涉及任何量化器（无记忆非线性）。Mounir & El-Mashade (2019) 摘要："Generally, companding techniques expand small signals while compressing large signals or compress large signals without affecting small signals... companding transforms that compress large signals without affecting small signals (such as, Log companding and Tanh companding) are better than the others from a BER point of view... the Log companding transform can be considered as the best practical companding transform among others." **[直接]** 出处：Mounir & El-Mashade, "On the selection of the best companding technique for PAPR reduction in OFDM systems", *Journal of Information and Telecommunication*, 2019, https://doi.org/10.1080/24751839.2019.1606878
佐证：μ 律（含参数 μ）作为 PAPR 压扩剖面被系统研究（PAPR/BER/PSD 随 μ 与峰比参数 PR 变化），见 GCU "An evaluation of modified µ-Law companding to reduce the PAPR of OFDM systems", https://researchonline.gcu.ac.uk/en/publications/an-evaluation-of-modified-%C2%B5-law-companding-to-reduce-the-papr-of-（**[直接]**，搜索摘要级）。

**2.2 先例们对小信号放大段的处理：回避而非接纳。**
上引 2019 综述把压扩变换分两类——「放大小信号」与「不影响小信号」，并实证后者 BER 更优：即该领域把 μ 律式小信号增益 > 1 视为需要规避/折衷的缺点，改进方向（如带峰比参数的 modified μ-law）正是为了压缩大信号而不放大小信号。**[解读]**（分类与结论为原文直接引文；「μ 律属于放大小信号类」由其原点斜率 46.0 > 1 直接推导，见 §4）。

**2.3 模拟域 μ 律压扩器先例：未找到「瞬时 μ 律传输曲线」的器件证据。**
模拟音频压扩器（G.162 类、MC33111、dbx/Dolby 谱系）全部是整流平均值 + 外部电容时间常数控制的可变增益（音节级），压缩比是 dB 域斜率（2:1），不是 G.711 连续 μ/A 律瞬时曲线；MC33111 手册全文无 μ 律表述（§1.6）。**[直接 + 解读]** 出处同 §1.6。

**2.4 音频工程域软饱和曲线族不含 μ/A 律。**
(a) Julius O. Smith III《Physical Audio Signal Processing》"Memoryless Nonlinearities" 章仅讨论硬削波、arctangent 软削波 f(x)=(2/π)atan(αx)、cubic 软削波、square-law/幂律，无 μ 律/A 律/G.711 表述：https://www.dsprelated.com/freebooks/pasp/Memoryless_Nonlinearities.html
(b) DAFX 2012 论文 "Harmonic Instability of Digital Soft Clipping Algorithms" 比较的 7 种算法为 Tanh(y=tanh(5x))、Sinusoidal、Exp2、Exp5、Two-Stage Quadratic（Zölzer）、Cubic、Reciprocal；全文无 μ/A 律：https://www.dafx.de/paper-archive/2012/papers/dafx12_submission_45.pdf
**[直接]**（两文献均为「清单不含 μ/A 律」的缺失性证据，非穷尽证明）。

**2.5 音频域最接近「模拟软限幅」的产品先例与对数律无关。**
Apogee Soft Limit（1991 年 AD-500 起的模拟电路，A-D 转换前圆化瞬态）："an analog circuit placed before the A-D conversion stage to gracefully round transients"；"input up to 4 additional dBu of analog input without triggering a digital over. The resultant digital level reads as +0.7 dBfs"；从 Threshold 以下 3–4 dB 开始圆化，配 Auto Makeup；**手册全文无对数律/μ 律特性表述**。**[直接]** 出处：Apogee *Soft Limit User's Guide* v1.0 (2022), https://knowledge.apogeedigital.com/hubfs/Users%20Guides%20and%20Documentation%20PDFs/Soft-Limit-Users-Guide.pdf

---

## 3. 反面证据（「对数律必须与量化器耦合、单独当波形曲线无意义」类论点）

**3.1 明确的成文反对论点：未找到。**
以多组检索（source_check + 定向抓取）查找文献中「对数律必须与量化器耦合、脱离量化器无意义」的明确表述，未获得任何一手或二手来源。这是**缺失证据**，不是「找到支持」——不能据此断言不存在此类论点。**[直接]**（检索未获证的事实）。

**3.2 间接限制性证据一：立法本义上对数律与量化器伴生。**
G.711 以判决值表立法（§1.1、§1.2），g711.tex 明言标准不采用连续公式（§1.3），Smith 1957 的「instantaneous companding」定义即「producing effectively nonuniform quantization」（§1.5）——在标准与原始文献语境中，连续律作为量化器的设计工具出现。**[直接引文 + 解读]** 出处同 §1。

**3.3 间接限制性证据二：波形域独立使用时小信号增益 > 1 被视为缺点。**
OFDM 文献偏好「不放大小信号」的压扩剖面（§2.2）；Wikipedia（二手汇总）："The μ-law algorithm provides a slightly larger dynamic range than the A-law at the cost of worse proportional distortions for small signals."，且 μ 律非线性量化等效增加动态范围 33 dB（5+1/2 bit），故压缩到 8 bit 需 14 bit 线性输入。**[直接引文]** 出处：https://en.wikipedia.org/wiki/Mu-law_algorithm

**3.4 间接限制性证据三：连续律无任何阈下单位增益段（见 §4.4）。**
作为「软饱和/限幅曲线」使用时，μ/A 律在定义域内部全程放大（仅端点单位增益）——这与限幅器「阈下透传」的语义相悖，是数学事实层面的结构性限制。**[推导]**（见 §4.4）。

---

## 4. 数学性质清单（连续律，逐条核对入册判据）

### 4.1 ⚠ 算术更正（显著）

| 量 | 既有文档写法 | 本文核算 | 结论 |
|---|---|---|---|
| μ 律原点斜率 μ/ln(1+μ) | mix-algo-survey.md §1-C 与地图 #8 正文「≈48.9」 | 255/ln(256) = 255/5.54518 = **45.99 ≈ 46.0**（20·log₁₀ = **33.25 dB**） | **48.9 为笔误**；正确值 46.0（33.25 dB） |
| A 律原点斜率 A/(1+lnA) | （地图 #8 未单列） | 87.6/(1+ln 87.6) = 87.6/5.47279 = **16.01 ≈ 16.0**（**24.1 dB**） | 无误；A=87.56 时 = 87.56/5.47233 = **16.00**（恰为 16，见 4.3） |
| Holbrook & Dixon 年份 | 票 #9 正文「1949」 | BSTJ 18(4), **1939** | **笔误**（见 §1.7） |

A 律参数两种写法并存：ITU-STL 手册正文记 **A=87.56**，G.711 语境与多数文献（含 Wikipedia A-law 条目 "In Europe, A = 87.6"）记 **87.6**。G.711 原文正文不含该数值（§1.2）。**[直接]** 出处：g711.tex、Wikipedia A-law（https://en.wikipedia.org/wiki/A-law_algorithm）、G.711 原文。

### 4.2 μ 律连续曲线（μ=255，x∈[−1,1]）

公式与反函数（Wikipedia 与 g711.tex 一致）："F(x) = sgn(x) ln(1+μ|x|)/ln(1+μ)"；"F⁻¹(y) = sgn(y) ((1+μ)^|y| − 1)/μ"。

| 性质 | 数值/结论 | 依据 |
|---|---|---|
| 值域 | [−1,1]，y(±1)=±1，奇函数 | 公式 **[推导]** |
| 严格单调 | y'(x) = μ/((1+μ·\|x\|)·ln(1+μ)) > 0 处处成立；曲线处处光滑（C∞） | **[推导]** |
| 有界 | \|y\| ≤ 1 | **[推导]** |
| 原点斜率 | **46.0**（33.25 dB），见 4.1 更正 | **[推导]** |
| 原点线性判据 | 原点一阶导存在且有限 ⇒ 小信号行为线性（y ≈ 46.0x）；但增益 ≫ 1 | **[推导]** |
| 小信号线性区 | \|x\| = 1/μ = 3.92e−3（−48.1 dBFS）处瞬时斜率已降至 23.0（减半，−6.02 dB）；线性近似的主导区远小于此 | **[推导]**；「主导区」边界定义不唯一 **[推断]** |
| 大信号渐近斜率 | y'(1) = μ/((1+μ)ln(1+μ)) = 255/1419.57 = **0.180**（−14.9 dB） | **[推导]** |
| 反函数 | 存在闭式（上式）；注意 g711.tex 所说「压缩无闭式」针对 G.711 折线表格实现，非连续律 | **[直接]**（公式）+ **[解读]** |

### 4.3 A 律连续曲线（A=87.6，x∈[−1,1]）

公式（Wikipedia，与 g711.tex 同构）："F(x) = sgn(x) { A|x|/(1+ln(A)), |x| < 1/A ; (1+ln(A|x|))/(1+ln(A)), 1/A ≤ |x| ≤ 1 }"。

| 性质 | 数值/结论 | 依据 |
|---|---|---|
| 值域 | [−1,1]，奇函数，y(±1)=±1 | **[推导]** |
| 严格单调 | 线性段斜率 16.0 > 0；对数段斜率 1/(x(1+lnA)) > 0；分段点 x=1/A 处两侧斜率相等（均 = 16.0），一阶导连续、二阶导跳变（曲率突变） | **[推导]** |
| 有界 | \|y\| ≤ 1 | **[推导]** |
| 原点严格线性段 | \|x\| < 1/A = 0.01142（−38.8 dBFS）内是**恒定斜率 16.0 的直线**（比 μ 律更严格意义的「原点线性」） | **[推导]** |
| 大信号渐近斜率 | y'(1) = 1/(1+lnA) = 1/5.47279 = **0.183**（−14.8 dB），与 μ 律端点 0.180 仅差 0.15 dB——两律在大信号端几乎重合，差异集中于中小信号段 | **[推导]** |
| A=87.56 的来历 | 87.56/(1+ln 87.56) = 16.00 恰好整——A=87.56 疑为使原点斜率为 16（折线段比 2:1 的幂）而选的值；**未见一手文献明言此动机** | 数值 **[推导]**；动机 **[推断]** |
| 反函数 | 存在闭式：线性段 x = \|y\|(1+lnA)/A（\|y\| < 0.183）；对数段 x = exp(\|y\|(1+lnA) − 1)/A（分段点连续性验证：\|y\|=1/(1+lnA) ⇒ x=1/A ✓） | **[推导]**（Wikipedia 逆式提取有排版疑点，本文按正函数反解并验证连续性） |

### 4.4 对判据最要紧的结构性事实：无阈下单位增益段

两条连续曲线均为端点 (0,0)、(1,1) 的**严格凹**（正半轴）映射 ⇒ 对一切 x∈(0,1) 有 **y > x**：即除 x=0 与 x=±1 外**全程增益 > 1**，不存在「阈下透传」段；μ 律全程增益 1 < g(x) ≤ 46.0，A 律 1 < g(x) ≤ 16.0。作为混音输出映射（s=a+b 归一化后过曲线），其语义是「全动态范围非线性放大器」，不是限幅器；大信号端仅斜率压缩（≈0.18），无饱和平台。**[推导]**（凹性 + 端点直接可得；「不是限幅器」为 **[解读]**）。

### 4.5 μ/A 两律对比速查

| 项 | μ 律 (255) | A 律 (87.6) |
|---|---|---|
| 原点斜率 | 46.0（33.25 dB） | 16.0（24.1 dB），差 9.2 dB |
| 原点线性形态 | 渐近线性（一阶近似） | 严格线性段至 −38.8 dBFS |
| 端点斜率 | 0.180 | 0.183 |
| 光滑性 | C∞ | 分段点二阶导跳变 |
| 线性量化等效 | ≈13 bit（μ 律输入需 14 bit） | ≈12 bit（输入需 13 bit） |

（末行 **[直接]**：g711.tex "in the dynamic range sense, A and μ law are equivalent to 12 and 13 bit linear quantization"；Wikipedia 33 dB/5.5 bit 表述同向。其余 **[推导]**。）

---

## 5. 对裁定票 #10 的影响

1. **判据核对结果**：连续 μ/A 律严格单调、有界（|y|≤1）、原点小信号线性（μ 律一阶斜率 46.0、A 律严格线性段 16.0），形式上满足「原点线性 + 单调有界」——不推翻「域公理为纲」判据；但「原点线性」必须解释为「一阶行为线性、增益 ≠ 1」，且 §4.4 表明曲线无任何阈下单位增益段，作为「软削波/限幅」语义存疑、作为「全动态域非线性映射」语义成立。
2. **「ITU 标准值」的准确措辞**：μ=255、A=87.6（手册正文 87.56）出自 STL 手册与文献惯例，G.711 正文只以判决值表立法——ADR 引用时宜写「STL 手册连续律公式 + ITU 参数」，不宜写「ITU 规定的连续曲线」。
3. **先例格局**：脱离量化器的先例在电信域成立（OFDM PAPR 压扩，无量化、波形域），但该文献把小信号放大视为缺点并偏好不放大小信号的变体；音频域软削波曲线族（tanh/arctan/sin/多项式）未含 μ/A 律——先例支持「无量化使用」，不支持「音频软饱和惯例」。
4. **数字更正须入 ADR**：μ/ln(1+μ) = 46.0（33.25 dB），既有 mix-algo-survey.md 与地图 #8 的「≈48.9」为笔误；A 律 87.56 与 87.6 两写法并存（87.56 使原点斜率恰为 16.00）；Holbrook & Dixon 年份为 1939。
5. **反面论点证据状态**：未找到「对数律必须与量化器耦合」的成文论点（缺失证据），#10 若引用此类反对须注明检索未获证；可引用的间接限制是「标准本体=判决值表」与「小信号增益被视为缺点」两条。

---

## 6. 未核实与缺失证据

- D2 channel bank 时代（1970 前后）选定 μ=255、A=87.6 的原始一手文献未取得（g711.tex 仅记 "The ITU chose the values..."；Wikipedia 仅记地域使用）。
- Smith 1957 与 Holbrook & Dixon 话音音量统计之间的直接引用链未逐条核实（OCR 残缺）。
- 音频域（JAES/AES）μ/A 律作软饱和的先例：多轮检索未获——缺失性证据，非不存在证明。
- 「对数律必须与量化器耦合」的成文反对：未获（§3.1）。
- dbx/Dolby 谱系一手文献（SOS 历史文 410 失效、mixonline 未核）——本清单以 G.162 与 MC33111 代替模拟压扩器证据。
- Infineon AN2095（μ 律模拟域应用笔记）原链接失效，未采。

## 7. 来源总表

**保留（一手/权威）**
1. ITU-T Rec. G.711 (11/1988) — 标准本体，判决值表立法、8 bit、13/14 bit 映射、负载容量。https://www.itu.int/rec/dologin_pub.asp?id=T-REC-G.711-198811-I%21%21PDF-E&lang=e
2. ITU-T STL-2000 手册 g711.tex（openitu/STL, dev 分支）— 连续律公式、SNR 公式、A=87.56、折线 15/13 段、「标准不规定该律」关键句。https://github.com/openitu/STL/blob/dev/doc/manual/g711.tex
3. B. Smith, BSTJ 36(3), 1957 — μ 律学术源头，「瞬时压扩=等效非均匀量化」。https://archive.org/details/bstj36-3-653
4. Holbrook & Dixon, BSTJ 18(4), 1939 — 负载额定/话音音量统计（年份更正依据）。http://archive.org/details/bstj18-4-624
5. ITU-T Rec. G.162 — 模拟压扩器立法（2:1、5/22.5 ms、降噪目的）。https://www.itu.int/rec/dologin_pub.asp?id=T-REC-G.162-198811-W%21%21PDF-E&lang=e
6. NXP/Motorola MC33111 数据手册 — 模拟 compandor 器件证据（无 μ 律表述）。https://www.nxp.com/docs/en/data-sheet/MC33111.pdf
7. Mounir & El-Mashade, J. Inf. Telecom. 2019 — OFDM 压扩变换分类与「不放大小信号更优」结论（脱离量化器先例）。https://doi.org/10.1080/24751839.2019.1606878
8. J. O. Smith, *Physical Audio Signal Processing*, "Memoryless Nonlinearities" — 音频软削波曲线族基线（无 μ/A 律）。https://www.dsprelated.com/freebooks/pasp/Memoryless_Nonlinearities.html
9. DAFX 2012, "Harmonic Instability of Digital Soft Clipping Algorithms" — 7 种软削波算法清单（无 μ/A 律）。https://www.dafx.de/paper-archive/2012/papers/dafx12_submission_45.pdf
10. Apogee *Soft Limit User's Guide* v1.0 (2022) — 音频域模拟软限幅产品先例（无对数律表述）。https://knowledge.apogeedigital.com/hubfs/Users%20Guides%20and%20Documentation%20PDFs/Soft-Limit-Users-Guide.pdf
11. Wikipedia μ-law / A-law（二手，仅用于公式/逆式、地域值、33 dB/5.5 bit、小信号失真对比等辅助事实，均已与 g711.tex 或推导交叉核对）。https://en.wikipedia.org/wiki/Mu-law_algorithm ；https://en.wikipedia.org/wiki/A-law_algorithm
12. GCU, "An evaluation of modified µ-Law companding to reduce the PAPR of OFDM systems"（次级佐证）。https://researchonline.gcu.ac.uk/en/publications/an-evaluation-of-modified-%C2%B5-law-companding-to-reduce-the-papr-of-

**弃用/降权**
- allaboutcircuits.com 技术博客 — 403 未核 + 二手科普。
- Sound On Sound "Inside Views: dbx" — 链接 410 失效。
- MDPI Electronics 文章 — 403 未核，与保留来源重复。
- Infineon AN2095 应用笔记 — 链接失效，未采。
- MC33111 最初假设「模拟 μ 律器件」 — 数据手册核查后推翻，改列为反例证据（§2.3）。
