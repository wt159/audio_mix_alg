# 审查修复报告 — integration/algorithm-family（HEAD a67d9b7 两轴发现）

**Commit**：`c1dc753` — `fix: 审查修复——注释编号/文档同步/登记表落地（两轴审查发现）`
**基线**：a67d9b7（`docs: 登记表首批门槛跑数实测（票 #6，全 PASS）`），6 文件，+134/−18 行。

---

## 逐项确认（按修复清单）

### 1. README.md L17 数字矛盾 — ✅
「七个逐样本公式」→「六个」，并补注三条动态链路的编号归属（[5] float 前瞻限幅、[8] softknee、[9] truepeak），与 L7 的「六个（链路 1/2/3/4/6/7）」完全一致。全文复查：`七个` 0 处、`六个` 2 处，无残留数字矛盾。

### 2. audio_mix.c L10 头注释 — ✅
「另加一条参考链路」改为「另加三条动态链路（[5] float 前瞻限幅、[8] softknee 压缩器、[9] truepeak 真峰值限幅）」，[5]/[8]/[9] 条目各注明「动态链路」与保护对象差异；块内 9 行算法索引的 `——` 分隔线同步统一对齐。措辞遵循仓库约定（英文标识符 + 中文说明）。
**偏差说明**：任务原文把 float 限幅器写作「[7]」，与清单第 3 项自身（「[7] 是 cubic」）矛盾，也与仓库全部既有编号矛盾——README 表 L5、`src/limiter.c:94`（`[5] 参考链路：… mix_float_limiter`）、`src/truepeak.c:194`（`与 [5] mix_float_limiter 的差别…`）均为 float 限幅器=[5]。故按仓库一致编号落地：**float 限幅器=[5]，softknee=[8]，truepeak=[9]**。

### 3. src/comp_soft.c L93 注释编号 — ✅
`[7] 软拐点压缩器` → `[8] 软拐点压缩器`（softknee 是 `algos[8]`，`[7]` 是 cubic）。全文件复查无其他错误编号。

### 4. src/truepeak.c L192 标签 + R128 年份统一 — ✅
L192 `[E]` → `[9]`。R128 引用统一为 **R128-2023**：`src/truepeak.c:22`（2020→2023），与 `src/truepeak.h:10` 一致；`docs/spec/algo-registry.md` §6.9 出处同引 2023。仓库现存 3 处带年份引用全部为 2023，其余 4 处「R128 §m」为不带年份的通称，不构成矛盾。

### 5. docs/spec/algo-registry.md §6 登记表本体 — ✅
新增「§6 登记表」，9 条链路各一节（6.1–6.9，顺序=链路编号 1,2,3,4,5,6,7,8,9），字段严格按 §1 声明逐行列出：名称/类型/公式出处/生态位/ctx 约定/参数默认值/指标实测/听感条款/验收状态。
- **指标实测**：48 kHz `--sec 2 --no-match` 实跑（`./audio_mix --sec 2 --no-match --outdir /tmp/amix_fix`），每节含峰值 dBFS/RMS/波峰因数/钳位/dBTP/波形残差/互调残差七项，动态链路另附增益统计（平均/最小 dB）。
- **听感/验收**：[7][8][9] 三个新算法=「免执行（首批）」+ PASS；[1]–[4]/[6] 五条 + [5] float 限幅器=「基准族」+「已收录」。
- **出处**：五个基准算法写「本仓库既有」（注明无文献公式，实现注记所在文件）；tanh/cubic 分别引 JOS PASP §9.1.6 与仓库既有实现注记；softknee 引 Giannoulis/Massberg/Reiss JAES 2012 Eq(4)/(15)/§2.4.1 Eq(20)；truepeak 引 BS.1770-4 (10/2015) Annex 2 §3 + EBU R128-2023 §m。

### 6. 表格输出分隔线加宽 — ✅
两处 `%s` 直字面分隔线（audio_mix.c:831、:847）由 90 列加宽至 **99 列**，与表头列宽和（22+9+8+9+9+9+9+12+12=99，含 dBTP 列）一致。纯外观，无行为变化。

### 7. Makefile shellcheck disable 注释 — ✅
两行 `# shellcheck disable=SC2283,SC2276 …` 注释删除，其余内容零改动。`make`/`make debug` 构建均不受影响（零警告）。

### 判断题不修（遵嘱）
cfg 统计四件套（spec §5 认可）、`active_samples` 字段、`main()` 收敛——未动。

---

## 验收结果

| 验收项 | 结果 |
|---|---|
| `make` 零警告（`-O2 -Wall -Wextra -pedantic`） | ✅ 0 warning/error |
| `./audio_mix --sec 1 --outdir /tmp/amix_fix_smoke` | ✅ exit 0，11 个 WAV |
| WAV 输入路径（两路 16-bit PCM） | ✅ exit 0，9 个 WAV |
| `./audio_mix_debug --sec 1 --outdir /tmp/amix_fix_dbg` ASan/UBSan | ✅ exit 0，无 AddressSanitizer/runtime error/LeakSanitizer 输出（WAV 输入路径同样干净） |
| README 无数字矛盾 | ✅ 「六个」×2，无「七个」 |
| 登记表 9 小节齐全 | ✅ `### 6.1`–`### 6.9` 共 9 节，字段 10×10（含 §1 模板行）逐项核对 |

## 关键数据（登记表实测依据，48 kHz `--sec 2 --no-match`）

叠加峰值 +4.5 dBFS；线性相加 dBTP **+0.1**、contra **+2.7**（样本间峰超标反例）；truepeak 链路 dBTP **−1.0**（≤ −0.7 门槛 PASS）、安全钳位 2；softknee 最小增益 −12.0 dB 无放大、样本峰 −5.8 dBFS；cubic 钳位 1390（与线性相加同样本集）。

## 残留风险

- **`[7]`/`[E]`→`[9]` 编号口径**：任务原文与仓库既有编号存在矛盾（见第 2 项偏差说明），本次按仓库一致口径落地；若两轴审查的原话另有所指（如把 float 限幅器整体重编号），需回票确认——但重编号会连锁改动 README/limiter.c/truepeak.c 三处既有交叉引用，超出本票范围。
- **Makefile 无 shellcheck 抑制后**，宿主 linter 仍会对 Makefile 报 SC2283/SC2276（bash 规则误用于 make 语法的 `VAR = value` 与 `$(SRCS:.c=.o)` 替换引用）。本机未安装 shellcheck，规则不适用于非 shell 文件，构建零警告不受影响；10 处发现已全部经 `lens_diagnostic_mark` 标记 false-positive。若后续接 CI 跑 shellcheck，需对 Makefile 配 `.shellcheckrc` 排除，而非恢复行内注释。
- 中途一次编辑曾误删 `for` 循环头与限幅器统计行，同轮内即发现并完整恢复；diff 复核（`git diff audio_mix.c`）确认最终只含两处分隔线与头注释改动，无残留。