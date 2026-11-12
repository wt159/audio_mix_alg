# AGENTS.md

C99 离线音频混音算法对比/A-B 试听工具，仅依赖 libm，不是实时混音库。入口 `audio_mix.c`（指标、WAV I/O、合成素材、表格输出、`main()`）；被测算法在 `src/mix_algos.{c,h}`，前瞻限幅器与 float 参考链路在 `src/limiter.{c,h}`。源码注释、README、CLI 输出均为中文；新增注释和文档用中文，标识符用英文。

## 构建与验证

- `make` 构建 `audio_mix`（`-O2 -Wall -Wextra -pedantic`，保持零警告）；源文件显式列在 Makefile 的 `SRCS`，新增 `.c` 要同步登记。
- `make debug` 构建 `audio_mix_debug`（`-O0 -g` + ASan/UBSan，独立 `*.dbg.o`，与 release 对象互不覆盖）；改算法、缓冲区或索引后运行 `./audio_mix_debug --sec 1 --outdir /tmp/opencode/amix_check_dbg`。
- 仓库没有自动化测试和 CI。改动后用短素材冒烟，检查指标表和退出码：
  `./audio_mix --sec 1 --outdir /tmp/opencode/amix_check`
  再验证 WAV 输入路径：
  `./audio_mix /tmp/opencode/amix_check/00_src_a.wav /tmp/opencode/amix_check/00_src_b.wav --outdir /tmp/opencode/amix_check2`
- 输出目录默认 `audio_mix_out/`，二进制、`*.o` 和输出均被 gitignore。WAV 是生成物，可能残留旧命名文件；当前文件名以 `main()` 的 `algos[]` 为准。

## 代码地图

`audio_mix.c` 按编号分节：0 工具、3 指标、4 WAV I/O、5 合成素材、6 表格输出、7 `main()`。算法语义和指标定义见 `README.md` 与入口文件头注释：

- `src/mix_algos.c`：逐样本公式（线性硬饱和、平均、防削波、contra、tanh 软饱和、μ/A 律对照），签名统一；
- `src/limiter.c`：前瞻限幅器状态机（`limiter_t`，不对外暴露）+ 参考链路 `mix_float_limiter`。

- 算法统一签名：`long fn(const int16_t *a, const int16_t *b, float *out, int n, void *ctx)`。输入 int16，输出 float `[-1,1]`，返回钳位样本数。
- 新增逐样本公式：`src/mix_algos.c` 实现 + `src/mix_algos.h` 声明；改限幅器链路则动 `src/limiter.c`。两者都要在 `main()` 的 `algos[]` 登记（名称、输出文件名、函数、ctx），并同步 `nalgo`、数组容量和 README 的算法表。
- 节目素材指标在响度对齐之后计算：`measure()` 的 `resid_db` 吸收整体增益；`resid_shape()` 再剥离 10 ms 慢变增益，用于公平比较限幅器。
- 限幅器统计在双音测试前存入 `lim_hard_prog/lim_avg_gain/lim_min_gain`，因为 `ctx` 会被双音测试复用并覆盖；调整这段顺序会静默改变表格输出。
- WAV 输入仅支持 16-bit PCM 单/双声道（双声道取 L/R 平均），两路必须同采样率，取较短长度；输出恒为单声道 16-bit。算法返回未量化的 float，最终 16-bit 转换统一走 `to_pcm16()`；逐样本公式内部的钳位是算法语义，并用于统计钳位样本数。

## 约定

- 默认响度对齐到 -18 dBFS 且只衰减不提升；`--no-match` 看真实电平。调算法时避免用对齐增益掩盖电平问题。
- 保持现有风格：C99、4 空格缩进、块首声明、`static` 函数；不引入新依赖或构建系统。
- 表格里的 `-inf` 表示完全线性（残差为零），不是错误。

## Agent skills

- **Issue tracker**：GitHub Issues（`wt159/audio_mix_alg`），用 `gh`；见 `docs/agents/issue-tracker.md`。
- **Triage labels**：默认五标签，与 canonical 角色同名；见 `docs/agents/triage-labels.md`。
- **Domain docs**：single-context，根 `GLOSSARY.md` + `docs/adr/`，懒创建；见 `docs/agents/domain.md`。
