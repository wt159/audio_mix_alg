# Research: G.711 对数律的立法本义、脱离量化器先例与数学性质（issue #9 事实清单）

交付文件：`docs/research/g711-log-law-facts.md`（工作树中已写入；分支提交/结票评论/地图更新由主管代做）。

## Summary
G.711 的立法载体是 8-bit 判决值表（折线近似），连续 μ/A 律公式是 ITU-STL 官方手册给出的数学定义且手册明言「标准不规定该形式」；脱离量化器使用连续对数律的先例存在于电信域（OFDM PAPR 压扩，无量化器、波形域），音频域软削波曲线族（tanh/arctan/多项式等）中未发现 μ/A 律先例；两曲线严格单调、有界、原点小信号线性，形式满足「原点线性 + 单调有界」判据，但在 (0,1) 内全程增益 >1、无阈下单位增益段。发现既有文档两处笔误：μ/ln(1+μ) ≈ 46.0（非 48.9）、Holbrook & Dixon 年份为 1939（非 1949）。

## Findings（关键事实，每条带出处；全文见交付文件）

1. **Claim:** G.711 (11/1988) §3.2 以判决值表（Tables 1a/1b、2a/2b）定义编码律，每样本 8 bit，正文不含 μ=255/A=87.6 数值。**Sources:** [ITU-T G.711](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-G.711-198811-I%21%21PDF-E&lang=e). **Support:** direct evidence（原文 PDF 全文提取）。**Confidence:** high.
2. **Claim:** STL-2000 手册 g711.tex 给出连续律公式与大信号 SNR 公式（SNR_μ=6.02B−9.99=38.17 dB），记 A=87.56、μ=255，并明言「The G.711 standard does not specify the law as defined above, but rather uses a good linear-piecewise approximation」（μ 律 15 段 / A 律 13 段）。**Sources:** [openitu/STL g711.tex](https://github.com/openitu/STL/blob/dev/doc/manual/g711.tex). **Support:** direct evidence（全文读取）。**Confidence:** high.
3. **Claim:** Smith 1957（BSTJ 36(3):653）定义「瞬时压扩 = 产生等效非均匀量化」，给出 μ 律公式与弱信号压扩增益 ≈ 等效增 4~6 bit 的结论——μ 律与量化器在学术源头即伴生。**Sources:** [BSTJ 36-3-653](https://archive.org/details/bstj36-3-653). **Support:** direct evidence（OCR 全文；公式处 OCR 残缺已标注）。**Confidence:** high.
4. **Claim:** 脱离量化器先例最强在 OFDM PAPR 压扩变换：μ/log 压扩直接作用于发射波形、无任何量化器；但该文献实证「压缩大信号而不放大小信号」的变体 BER 更优，即把 μ 律式小信号增益 >1 视为需规避的缺点。**Sources:** [Mounir & El-Mashade 2019](https://doi.org/10.1080/24751839.2019.1606878)（摘要直接引文）。**Support:** direct evidence（分类结论）+ interpretation（μ 律归属「放大类」由原点斜率推导）。**Confidence:** high.
5. **Claim:** 模拟压扩器是另一谱系：G.162 规定 dB 域 2:1 与 5/22.5 ms 作用/恢复时间；MC33111 手册全文无 μ 律/G.711 表述（整流平均值 + 电容时间常数的可变增益）——未找到「瞬时 μ 律传输曲线」的模拟器件证据。**Sources:** [ITU-T G.162](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-G.162-198811-W%21%21PDF-E&lang=e)、[MC33111](https://www.nxp.com/docs/en/data-sheet/MC33111.pdf). **Support:** direct evidence。**Confidence:** high.
6. **Claim:** 音频域软削波曲线族不含 μ/A 律：PASP "Memoryless Nonlinearities"（硬削波/atan/cubic/幂律）与 DAFX 2012 软削波算法比较（Tanh/Sin/Exp2/Exp5/TSQ/Cubic/Reciprocal）均无 μ/A 律；Apogee Soft Limit（模拟软限幅产品先例）手册亦无对数律表述。**Sources:** [PASP](https://www.dsprelated.com/freebooks/pasp/Memoryless_Nonlinearities.html)、[DAFX 2012](https://www.dafx.de/paper-archive/2012/papers/dafx12_submission_45.pdf)、[Apogee Soft Limit Guide](https://knowledge.apogeedigital.com/hubfs/Users%20Guides%20and%20Documentation%20PDFs/Soft-Limit-Users-Guide.pdf). **Support:** direct evidence（缺失性证据，非穷尽证明）。**Confidence:** high（就所查文献而言）。
7. **Claim（⚠ 算术更正）:** μ/ln(1+μ) = 255/ln(256) = 255/5.54518 = **45.99 ≈ 46.0**（33.25 dB）；既有 mix-algo-survey.md §1-C 与地图 #8 的「≈48.9」为笔误。A 律原点斜率 87.6/(1+ln 87.6) = 16.0（24.1 dB）无误；A=87.56 时恰为 16.00。**Support:** direct evidence（公式）+ 研究者推导（数值）。**Confidence:** high.
8. **Claim（数学性质）:** 两曲线均严格单调、有界（|y|≤1）、原点小信号线性（μ 律一阶斜率 46.0；A 律在 |x|<1/A≈0.0114 内严格线性、斜率 16.0）；大信号端点斜率 μ 律 0.180 / A 律 0.183（仅差 0.15 dB）；反函数均有闭式。关键结构性事实：凹性 + 端点 (0,0)(1,1) ⇒ 对一切 x∈(0,1) 有 y>x，**除端点外全程增益 >1，无任何阈下单位增益段**。**Support:** 研究者推导（由 STL/Wikipedia 公式直接推导，交付文件 §4 逐项列出）。**Confidence:** high.
9. **Claim（⚠ 年份更正）:** Holbrook & Dixon "Load Rating Theory for Multichannel Amplifiers" 为 BSTJ 18(4), **1939**（票 #9 正文所写 1949 为笔误）。**Sources:** [archive.org bstj18-4-624](http://archive.org/details/bstj18-4-624). **Support:** direct evidence（卷期元数据）。**Confidence:** high.

## Contradictions
- 未发现「对数律必须与量化器耦合」的成文反对论点（缺失证据，非不存在证明）；间接限制性证据：标准本体=判决值表（事实 1）、OFDM 文献视小信号放大为缺点（事实 4）。
- A 律参数 87.56（STL 手册正文）与 87.6（文献惯例/Wikipedia）两写法并存，非矛盾，ADR 需并列注明；G.711 原文正文不含该数值。

## Missing evidence
- μ=255/A=87.6 的选定一手文献（D2 channel bank 时代）未取得；Smith 1957 ↔ Holbrook & Dixon 话音音量统计的直接引用链未逐条核实（OCR 残缺）；JAES/AES 域 μ/A 律软饱和先例未获（缺失性证据）；「对数律必须与量化器耦合」成文论点未获；dbx/Dolby 一手文献未核（以 G.162 + MC33111 代替）。

## Sources
- Kept: 交付文件 §7 完整列出 12 项保留来源（ITU G.711、STL g711.tex、Smith 1957、Holbrook & Dixon 1939、ITU G.162、MC33111、Mounir & El-Mashade 2019、PASP、DAFX 2012、Apogee、Wikipedia μ/A-law、GCU 佐证）。
- Rejected/deprioritized: allaboutcircuits（403/二手）、SOS dbx 文（链接失效）、MDPI 文（403/重复）、Infineon AN2095（链接失效）。

## Next steps
- 主管代做：建分支 `research/g711-log-law-facts` 提交交付文件 → `gh issue comment 9`（清单要点 + 文件指针，显著标注 46.0/1939 两处更正）→ `gh issue close 9` → 地图 #8「Decisions so far」追加决策索引行（gist：连续律=STL 手册数学定义而标准以判决值表立法；OFDM 为无量化先例但视小信号增益为缺点；曲线无阈下单位增益段；μ 律原点斜率应更正为 46.0）。

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "事实清单文档已写入 docs/research/g711-log-law-facts.md（约 9KB，7 节：摘要/立法本义/先例/反面证据/数学性质/对 #10 影响/未核实与来源），另在本权威输出路径存档完整研究简报；每条事实带出处与证据类型标注，含 μ/ln(1+μ)=46.0（非 48.9）与 Holbrook & Dixon 1939（非 1949）两处显著更正。"
    }
  ],
  "changedFiles": [
    "docs/research/g711-log-law-facts.md"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "web 调研（fetch_content/source_check，无 shell）",
      "result": "passed",
      "summary": "取得并核验 12 项保留来源，含 ITU G.711/G.162 原文 PDF、STL g711.tex 全文、Smith 1957 OCR 全文、MC33111 手册、PASP/DAFX 软削波基线、Apogee 手册、Wikipedia μ/A 律公式"
    }
  ],
  "validationOutput": [
    "算术自查：255/ln(256)=255/5.54518=45.99≈46.0（20·log₁₀=33.25 dB）；87.6/5.47279=16.01；87.56/5.47233=16.00；A 律端点斜率 1/(1+ln 87.6)=0.183，μ 律 255/1419.57=0.180",
    "写入成功：write 工具返回 ✓ Markdown clean（3103ms）；pi-lens 自动检查仅 MD060×8/MD036×2 markdown 风格提示（ℹ️ 无需处理，不影响事实内容）"
  ],
  "residualRisks": [
    "μ=255/A=87.6 选定的一手文献（D2 channel bank 时代）未取得，ADR 引用参数出处只能到 STL 手册/文献惯例层",
    "音频域「无 μ/A 律软饱和先例」为缺失性证据（PASP/DAFX/Apogee 三处清单均不含），非穷尽证明；JAES 期刊库未直接检索",
    "Smith 1957 与 Holbrook & Dixon 的直接引用链因 OCR 残缺未逐条核实",
    "交付文件目前在工作树中未提交（无 git/shell/gh 工具），分支提交、结票评论、闭票、地图 #8 更新均待主管代做；若主管直接用该工作树文件，内容即为最终版"
  ],
  "noStagedFiles": true,
  "diffSummary": "新增 docs/research/g711-log-law-facts.md：对齐票 #9 四小节的中文事实清单（每条带出处与证据类型），显著标注 μ/ln(1+μ)=46.0 与 Holbrook & Dixon 1939 两处既有文档笔误，文末含「对裁定票 #10 的影响」5 条与未核实清单。",
  "reviewFindings": [
    "blocker: 无",
    "注意：主工作树此前若有未提交改动，主管建分支时应使用 git worktree 临时工作区（原任务步骤 3 的约束仍适用）"
  ],
  "manualNotes": "研究票 #9 的调研与文档产出已完成；未执行任何 git/gh/shell 操作（工具不可用，收尾由主管代做）。关键事实摘要：① G.711 以判决值表立法，连续律公式是 STL 手册数学定义且明言标准不采用该形式；② Smith 1957 源头定义「瞬时压扩=等效非均匀量化」；③ 脱离量化器先例在 OFDM PAPR 压扩（无量化、波形域），但该文献视小信号放大为缺点；④ 模拟压扩器（G.162/MC33111）是时变增益谱系，与瞬时对数曲线不同类；⑤ 连续 μ/A 律单调有界、原点线性（斜率 46.0/16.0），但在 (0,1) 内全程增益>1、无阈下单位增益段——不推翻「域公理为纲」判据，但影响 #10 对语义（非线性映射 vs 限幅器）的裁定措辞。"
}
```
