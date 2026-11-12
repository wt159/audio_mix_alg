# Standards 审查（integration/algorithm-family，vs main）

## 硬违规
1. README.md:17「七个逐样本公式」与 :12「六个」矛盾（实为 6）——违反 AGENTS.md「同步 README 算法表」。
2. audio_mix.c:10 头注「另加一条参考链路」过时：动态链路已三条（[5][8][9]）。
3. src/comp_soft.c:93 注释「[7] 软拐点压缩器」编号错，应为 [8]，与 cubic 撞号。
4. src/truepeak.c:192 编号「[E]」应为 [9]；truepeak.h:10 与 truepeak.c:22 的 R128 年份（2023/2020）引用不一致。

## 基线 smell（判断题）
- Duplicated Code / Data Clumps：新 cfg 复制 limiter 统计四件套；main() 三份「双音前存统计」（:602、:783–789）——docs/spec §5 认可该流程，判可接受。
- Speculative Generality：两 cfg 的 active_samples 有写无读；Makefile:1 shellcheck disable 超仓库规定工具链。
- Divergent Change：main() 注册/统计/打印区本分支第三次扩张，宜后续收敛。

## 通过
零警告实测；SRCS/头依赖/algos[9]/nalgo/README 表/手动编译行全同步；统一签名、static、块首声明、4 空格、中文注释合规；统计保存顺序约定保持；GLOSSARY/ADR 合规。残留：表格分隔线 90 列未随 99 列行加宽（外观）。