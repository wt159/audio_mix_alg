# Spec 轴只读审查（integration/algorithm-family, 5 提交）

## 一、spec 要求但缺失/半成品
1. **登记表结构未按 §1 落实**：§1 L8 "每算法一节，字段固定"（名称/类型/公式出处/生态位/ctx 约定/参数默认值/指标实测/听感条款/验收状态）。实际 §2.2–2.4 把字段揉进"实现状态"散文，无独立字段行；**§2.4（A）连实现状态段都没有**，仅 §2.5 表一行；"指标实测"字段要求 resid_db/resid_shape/RMS/波峰因数等，§2.5 只给了门槛数，全字段未入表——半成品。
2. §3 听感"状态列填免执行"：无独立状态列，仅文字括注，勉强算占位。

## 二、spec 未要求的（scope creep）
1. 链路重编号：float 限幅器 06→07（WAV 文件名变更，将残留旧 06 文件，见 AGENTS.md 警告）；spec 未要求。
2. Makefile 加 shellcheck disable 注释，纯噪声。
3. 文案不同步（属同步义务的缺陷）：README L7 "六个逐样本公式" vs L17 "七个"自相矛盾；audio_mix.c 头注释 L10 仍写"另加一条参考链路"，实为三条动态链路。

## 三、疑似实现错误核对（均未发现实质错误）
- E：48 阶 4 相位系数逐项对上 BS.1770-4 表（相位 0/1 直流增益 1.0016/0.9730 自洽）；独立重算：满幅正弦 +0.01 dBTP、方波 +2.00 dBTP、峰间正弦 +0.01，与 ADR-0003 锚点一致；--sec 2 --no-match 实测 dBTP=−1.0≤−0.7 PASS；float 域省 12.04 dB 有 Annex 2 原文支撑。
- D：Eq(4) 三分支等价正确（over 已排除 <−W 后，|2(xG−T)|≤W 成立）；Eq(15) 释放支路 αR·yL 纯放电正确；g_db>0 硬钳保只衰减（−12.0 dB，峰 −5.8 dBFS≤0 PASS）；统计双音前保存 ✓。
- A：三分段公式、|x|>1 计钳位（1390，与线性同集）、带宽=3 入 GLOSSARY ✓。
- dBTP 列对全部算法、对齐后计算 ✓；algos[9]/nalgo/Makefile SRCS/README/GLOSSARY/ADR-0002/0003 齐备；make 零警告、debug 冒烟（--sec 1 + WAV 路径）干净。
- 残留小疑点：truepeak 安全钳位钳到 ceiling（0.891）而非 limiter.c 的 ±1.0，hard_clip 语义与 [5] 不可直接对比（spec 未约束）。

**结论**：代码实现与门槛全部达标；主要欠账在 spec 文档自身——§1 登记表结构未逐字段落地（尤其 A），及三处文档不同步。

验证：make 零警告；./audio_mix --sec 1/2 --no-match、WAV 输入路径、debug ASan 冒烟均 PASS；dBTP 独立复核一致；仓库零改动。