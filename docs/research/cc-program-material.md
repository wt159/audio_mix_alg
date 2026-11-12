# 调研：CC 节目素材落库（人声 + 音乐各一段）

> 票 #18 交付物。目标：为盲听协议与文件包搜寻两段可自由许可的节目素材（一段人声为主、一段音乐），落成 16-bit PCM WAV 入库 `assets/`。每条结论附来源；许可证以来源页面本身为准（逐一核过，未采信第三方转述）。

## 1. 选材结论

| 素材 | 文件 | 角色 | 时长 |
|---|---|---|---|
| 人声（朗读，无背景音乐） | `assets/cc_voice.wav` | 盲听协议人声路 | 48.00 s |
| 音乐（钢琴独奏） | `assets/cc_music.wav` | 盲听协议音乐路 | 48.00 s |

两段均裁自 Wikimedia Commons 提供的**无损 PCM 原件**（WAV / FLAC），处理链全程无损代际（见 §3），采样率一致（44100 Hz，两段原始素材同为 44100 Hz，未重采样）。

## 2. 来源与许可证（逐项核自来源页面）

### 2.1 人声 `assets/cc_voice.wav`

| 项 | 值 |
|---|---|
| Commons 文件页 | <https://commons.wikimedia.org/wiki/File:Spoken_Wikipedia_-_Eleventh_Avenue_(Manhattan).wav> |
| 原件 URL | <https://upload.wikimedia.org/wikipedia/commons/5/54/Spoken_Wikipedia_-_Eleventh_Avenue_%28Manhattan%29.wav> |
| 标题 | Spoken Wikipedia — Eleventh Avenue (Manhattan)（英文维基百科条目朗读，口齿清晰的英文朗读、无背景音乐） |
| 许可证 | **CC BY-SA 4.0**（Commons 文件页声明「Creative Commons Attribution-Share Alike 4.0」，含署名与相同方式共享条款） |
| 作者 / 朗读者 | 朗读者 JohnAnkerBow（上传者本人录音，2018-11-15 上传）；所读条目作者见 Commons 页 Credit 字段 |
| 上传日期 | 2018-11-15（Commons 文件历史） |
| 下载日期 | **2026-11-12**（UTC） |
| 原件参数 | 44100 Hz / 16-bit / 立体声 / 785.8 s / WAV（本次实测 soundfile 0.14.0 + libsndfile 1.2.2） |

### 2.2 音乐 `assets/cc_music.wav`

| 项 | 值 |
|---|---|
| Commons 文件页 | <https://commons.wikimedia.org/wiki/File:Schubert_-_Piano_Sonata_No._13_in_A_major,_D664_-_II._Andante_(Paul_Pitman).flac> |
| 原件 URL | <https://upload.wikimedia.org/wikipedia/commons/d/df/Schubert_-_Piano_Sonata_No._13_in_A_major%2C_D664_-_II._Andante_%28Paul_Pitman%29.flac> |
| 标题 | Schubert — Piano Sonata No. 13 in A major, D 664, II. Andante（钢琴独奏，Musopen 录音） |
| 许可证 | **公有领域**（Commons 文件页 Recording 段：「This work has been released into the public domain by its author, Musopen」；乐谱作曲家 Franz Schubert 1797–1828 亦为公有领域） |
| 作者 | 作曲 Franz Schubert；钢琴演奏 Paul Pitman；录音由 Musopen（非营利组织）以公有领域发布 |
| 来源组织 | Musopen（<https://musopen.org/music/7269-piano-sonata-in-a-major-d-664/>），Commons 页附 Musopen 上传须声明公有领域的说明 |
| 上传日期 | 2024-11-12（Commons 文件历史，用户 Kcx36） |
| 下载日期 | **2026-11-12**（UTC） |
| 原件参数 | 44100 Hz / 24-bit / 立体声 / 290.8 s / FLAC（soundfile 实测；下转 16-bit 属位深截断，无损采样率链） |

## 3. 处理链（含代际披露）

**全程无损/PCM 原件链，无有损解码代际**：

1. 直接下载 Commons 原件（人声 = 16-bit PCM WAV；音乐 = 24-bit FLAC，均未经过 MP3/AAC/Vorbis 等有损环节）。
2. 用 soundfile（libsndfile 1.2.2）读入 float64；立体声取 L/R 均值转单声道（票内明确允许「立体声可由工具取 L/R 均值转单声道」）。
3. 选段：0.05 s 子窗 RMS 扫描整段，选「有效话音占比 ≥99%」的 48 s 窗（人声，起点吸附到 120.00 s 附近局部能量最低点，避免切在音节中间）；音乐取连续演奏段 120.5–168.5 s。
4. 写出 16-bit PCM WAV（`PCM_16`），**未施加任何增益、归一化或动态处理**（响度对齐由冒烟命令的 `--match` 在算法侧完成）。

**披露**：
- 音乐段为 24-bit → 16-bit 位深截断（线性量化，无重采样、无有损编解码）；人声段原生 16-bit，无位深变化。
- 两段均未重采样：原始与成品采样率同为 44100 Hz（票允许 44.1 kHz；48 kHz 优先但 Commons 可自由许可的 48 kHz 无损人声/音乐原件稀缺，本次未找到满足全部条件的 48 kHz 候选，采 44.1 kHz 兜底是票内明文允许的）。
- **无有损代际**：未使用任何 MP3/AAC/OGG 解码链。票内「高码率有损兜底」路径未启用。

## 4. 成品实测参数（soundfile 0.14.0 / libsndfile 1.2.2 / numpy 2.4.4）

| 项 | `assets/cc_voice.wav` | `assets/cc_music.wav` |
|---|---|---|
| 采样率 | 44100 Hz | 44100 Hz（两段一致 ✓） |
| 位深 | 16-bit PCM（`PCM_16`） | 16-bit PCM（`PCM_16`） |
| 声道 | 单声道（L/R 均值） | 单声道（L/R 均值） |
| 时长 | 48.00 s | 48.00 s |
| 峰值电平 | **-4.560 dBFS** | **-3.261 dBFS** |
| 满幅样本数（\|x\|≥1-1/32768） | 0 | 0 |
| 最长平顶连串（\|x\|≥0.999） | 0 | 0 |
| 结论 | 不过载、无数字削波 | 不过载、无数字削波 |

两文件各 4.0 MB，gitignore 未覆盖 `assets/`（`git check-ignore` 退出码 1，已验证）。

## 5. 冒烟验证（票内验收命令）

```text
$ make                                    # 零警告，构建通过
$ ./audio_mix assets/cc_voice.wav assets/cc_music.wav --match -18 --outdir /tmp/amix_listen_smoke
素材: assets/cc_voice.wav + assets/cc_music.wav（44100 Hz, 48.00 s, 取较短长度）
响度对齐: 开启（目标 -18 dBFS，只衰减不提升）
…（11 个方案指标表全部输出，无 NaN/异常值；限幅器/压缩器/真峰值统计行正常）
EXIT=0
```

**PASS**：退出码 0；两路输入被识别为 44100 Hz、48.00 s 同采样率；`--match -18` 响度对齐生效（各方案匹配增益 -12.0 ～ 0.0 dB）；11 个方案的峰值/RMS/波峰因数/dBTP/残差指标全部为有限值，仅「平均」「限幅」链路的波形残差按预期出现 `-inf`（README 明示该值代表完全线性，非错误）。限幅器统计（平均增益 0.0 dB、最小增益 0.0 dB、安全钳位 0）表明素材本身不过载，限幅器未被触发硬钳位——与 §4 峰值实测一致。

## 6. 许可合规说明

- 人声段 CC BY-SA 4.0：仓库内分发需署名（朗读者 JohnAnkerBow + Commons 文件页链接 + 许可证链接）并注明修改（裁剪/单声道/选段）。本文件 §2.1 即署名记录；后续打包票（#20）在分发文件包时应随包携带该署名。
- 音乐段公有领域（Musopen 录音 + Schubert 乐谱双公有领域）：无署名义务，但 §2.2 保留来源记录以便溯源。
- 两段均采自 Wikimedia Commons 原件服务器（upload.wikimedia.org），非第三方转述；许可证以 Commons 文件页 extmetadata（`License`/`UsageTerms`）与页面声明为准，已于下载当日核验。
