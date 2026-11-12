全部步骤完成。最终报告：

## 产出报告

| 项 | 值 |
|---|---|
| **结票链接** | https://github.com/wt159/audio_mix_alg/issues/2#issuecomment-6813814547（#2 已关闭） |
| **分支名** | `research/mix-algo-survey`（commit `7fd9b41`，已推送，不开 PR，已切回 main） |
| **调研文件路径** | `docs/research/mix-algo-survey.md`（203 行，中文，每条结论带一手引用） |
| **地图更新** | #1 “已做出的决策” 已追加决策行（先取 `--json body` 再全量替换，其它节未动） |

**排序建议前 3**（指标可比性 × 实现成本 × 生态位独特性）：
1. **真峰值限幅器（true-peak limiter）** — ITU-R BS.1770-4 Annex 2（4× 过采样 + 48 阶 4 相位 FIR，系数表一手核对）+ EBU R128 条款 m) −1 dBTP；唯一样本间峰值防护，仓库输出即 16-bit WAV 是真实下游场景；~100 行需状态。
2. **软拐点压缩器** — Giannoulis/Massberg/Reiss, JAES 60(6) 2012, §2.2 Eq(4) 软拐点 + §2.3.3 Eq(15) 分支峰值检测；与现有链路 5 同指标框架公平 A/B；~60–80 行需状态。
3. **三次多项式软削波器（cubic soft clipper）** — JOS *Physical Audio Signal Processing*（§9.1.6，`f(x)=x−x³/3`）；tanh 的代数族对照面，带宽扩展=3 为奇非线性最低混叠率；~20 行无状态。

8 个候选**全部可装**统一签名（有状态链路学 `mix_float_limiter` 把状态藏 ctx）。新问题 4 条只写进了结票评论（dBTP 指标列、样本间峰测试素材、μ 律语义裁定、变体归档粒度），未自行建票。工作区仅剩未跟踪 `.pi/` 工具目录，src/ WIP 未受任何影响。