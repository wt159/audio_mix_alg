#!/usr/bin/env python3
# 盲听文件包生成器（wayfinder 票 #20；协议 = 票 #19 决议）。
# 用法：python3 tools/make_listen_package.py [--seed N] [--force]
# 可复现：同 seed 重跑目录结构与音频逐字节一致（音频由确定性 C 工具生成）。

import argparse
import json
import os
import random
import secrets
import shutil
import subprocess
import sys
import tempfile
from itertools import product

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PKG = os.path.join(REPO, "listen_package")
KEY_PATH = os.path.join(REPO, "listen_package.key.json")
CONTENT = os.path.join(PKG, ".content")

# 链路 → 工具输出文件名（登记表 §6；[[...]] 内不写入任何交付文档）
CHAIN_FILES = {
    "basis": "01_sum_clip.wav",      # [1] 线性相加+硬饱和（基准）
    "cubic": "06_cubic_soft.wav",    # [7] 三次软削波
    "softknee": "08_softknee.wav",   # [8] softknee 压缩器
    "truepeak": "09_truepeak.wav",   # [9] truepeak 前瞻限幅
    "float": "07_float_limiter.wav", # [5] float 前瞻限幅（探索组）
}
GROUP_VARIANTS = ["cubic", "softknee", "truepeak"]  # 组 → 被测链路（随机指派）
MATERIALS = ["voice", "music"]
LETTERS = "CDEFGHJKLMNPQRSTUVWXYZ"  # 去掉 A/B/I/O：单字母透盲 + 视觉易混
GUIDE = ("选你在听感上更接受的一段——失真/毛刺更少、更自然；"
         "无法分辨时任意选，统计天然吸收随机性（真无差异时期望五五开，落在「不劣于」带）。")
N_ROUNDS = 5
EXPLORE_QUESTIONS = [
    "两段能否分辨？（可辨 / 不可辨）",
    "若可辨：倾向哪边（写代号）+ 一句感受。",
]


def ensure_dir(path: str) -> None:
    try:
        os.makedirs(path, exist_ok=True)
    except OSError as e:
        sys.exit(f"error: cannot create directory {path}: {e}")


def fs_rmtree(path: str) -> None:
    try:
        shutil.rmtree(path)
    except OSError as e:
        sys.exit(f"error: cannot remove {path}: {e}")


def write_text(path: str, text: str) -> None:
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
    except OSError as e:
        sys.exit(f"error: cannot write {path}: {e}")


def dir_codes(path: str) -> list:
    try:
        names = os.listdir(path)
    except OSError as e:
        sys.exit(f"error: cannot list {path}: {e}")
    return sorted(os.path.splitext(f)[0] for f in names if f.endswith(".wav"))


def run_tool(material: str) -> dict:
    """素材自混跑全部链路：./audio_mix <素材> <素材> --match -18（票 #20 正文约定）。"""
    src = os.path.join(REPO, "assets", f"cc_{material}.wav")
    try:
        outdir = tempfile.mkdtemp(prefix=f"pkg_build_{material}_")
    except OSError as e:
        sys.exit(f"error: cannot create build directory: {e}")
    subprocess.run(["./audio_mix", src, src, "--match", "-18", "--outdir", outdir],
                   cwd=REPO, check=True)
    return {k: os.path.join(outdir, v) for k, v in CHAIN_FILES.items()}


def fresh_codes(rng: random.Random, n: int, used: set) -> list:
    pool = ["".join(c) for c in product(LETTERS, repeat=3) if "".join(c) not in used]
    picks = rng.sample(pool, n)
    used.update(picks)
    return picks


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=None, help="随机种子（缺省新取，记录进钥匙）")
    ap.add_argument("--force", action="store_true", help="覆盖已存在的 listen_package/")
    args = ap.parse_args()
    seed = args.seed if args.seed is not None else secrets.randbits(48)
    rng = random.Random(seed)

    if os.path.exists(PKG):
        if not args.force:
            print("listen_package/ 已存在；确认无误可加 --force 覆盖", file=sys.stderr)
            return 1
        fs_rmtree(PKG)
    ensure_dir(PKG)
    ensure_dir(CONTENT)

    # 1) 跑工具：每素材自混一批
    src = {m: run_tool(m) for m in MATERIALS}

    # 2) 组 ↔ 链路随机指派；块（组×素材）听序随机
    variants = GROUP_VARIANTS[:]
    rng.shuffle(variants)
    group_map = {f"g{i + 1}": variants[i] for i in range(3)}
    cells = [(g, m) for g in group_map for m in MATERIALS]
    rng.shuffle(cells)

    # 3) 内容文件（真实文件，中性命名）+ 每轮硬链接随机代号摆位
    used = set()
    content_files = []
    rounds_key = []
    for g, m in cells:
        gdir = f"{g}_{m}"
        p = os.path.join(CONTENT, f"{gdir}_p.wav")
        q = os.path.join(CONTENT, f"{gdir}_q.wav")
        shutil.copyfile(src[m]["basis"], p)
        shutil.copyfile(src[m][group_map[g]], q)
        content_files += [p, q]
        for r in range(1, N_ROUNDS + 1):
            code_p, code_q = fresh_codes(rng, 2, used)
            rdir = os.path.join(PKG, gdir, f"round{r}")
            ensure_dir(rdir)
            basis_is_p = rng.randrange(2) == 0
            basis_code = code_p if basis_is_p else code_q
            variant_code = code_q if basis_is_p else code_p
            os.link(p, os.path.join(rdir, basis_code + ".wav"))
            os.link(q, os.path.join(rdir, variant_code + ".wav"))
            rounds_key.append({"block": gdir, "round": r,
                               "basis_code": basis_code,
                               "variant_code": variant_code})

    # 4) 探索组（每素材一轮，正式块判完后再听）
    explore_key = []
    for m in MATERIALS:
        edir = f"explore_{m}"
        p = os.path.join(CONTENT, f"{edir}_p.wav")
        q = os.path.join(CONTENT, f"{edir}_q.wav")
        shutil.copyfile(src[m]["float"], p)
        shutil.copyfile(src[m]["truepeak"], q)
        content_files += [p, q]
        code_p, code_q = fresh_codes(rng, 2, used)
        ensure_dir(os.path.join(PKG, edir))
        os.link(p, os.path.join(PKG, edir, code_p + ".wav"))
        os.link(q, os.path.join(PKG, edir, code_q + ".wav"))
        explore_key.append({"dir": edir, "material": m,
                            "float_code": code_p, "truepeak_code": code_q})

    listen_order = [f"{g}_{m}" for g, m in cells] + [f"explore_{m}" for m in MATERIALS]

    # 5) 钥匙（仓库根单独文件；试听人判完前不得打开）
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO,
                          capture_output=True, text=True, check=True).stdout.strip()
    key = {
        "seed": seed,
        "warning": "解盲钥匙——全部判定记录完成后方可查看（票 #19：全判完统一解盲）",
        "generated_from": head,
        "tool_args": ["--match", "-18"],
        "materials": {m: f"assets/cc_{m}.wav" for m in MATERIALS},
        "chain_files": CHAIN_FILES,
        "groups": {g: {"variant": group_map[g], "chain_file": CHAIN_FILES[group_map[g]]}
                   for g in group_map},
        "listening_order": listen_order,
        "rounds": rounds_key,
        "explore": explore_key,
    }
    write_text(KEY_PATH, json.dumps(key, ensure_ascii=False, indent=2) + "\n")

    # 6) 试听指引 / 记录表 / 署名
    write_docs(listen_order)

    # 7) 静默自检（按计划结构核对，不打印任何代号→链路对应）
    all_codes = [c for rk in rounds_key for c in (rk["basis_code"], rk["variant_code"])]
    all_codes += [c for ek in explore_key for c in (ek["float_code"], ek["truepeak_code"])]
    expect = N_ROUNDS * 2 * len(GROUP_VARIANTS) * len(MATERIALS) + 2 * len(MATERIALS)
    if len(all_codes) != expect:
        sys.exit(f"error: 代号总数异常: {len(all_codes)} != {expect}")
    if len(set(all_codes)) != len(all_codes):
        sys.exit("error: 代号不唯一")
    for rk in rounds_key:
        for c in (rk["basis_code"], rk["variant_code"]):
            p = os.path.join(PKG, rk["block"], f"round{rk['round']}", c + ".wav")
            if not os.path.isfile(p):
                sys.exit(f"error: round file missing: {rk['block']}/round{rk['round']}/{c}")
    for ek in explore_key:
        for c in (ek["float_code"], ek["truepeak_code"]):
            p = os.path.join(PKG, ek["dir"], c + ".wav")
            if not os.path.isfile(p):
                sys.exit(f"error: explore file missing: {ek['dir']}/{c}")
    for p in content_files:
        try:
            st = os.stat(p)
        except OSError:
            sys.exit(f"error: content file missing: {p}")
        if st.st_nlink < 2:
            sys.exit(f"error: hard link not in place: {p}")

    print(f"seed={seed}")
    print(f"blocks={len(cells)} rounds/block={N_ROUNDS} explore_dirs=2 total_code_files={len(all_codes)}")
    print("listen_order=" + " -> ".join(listen_order))
    print(f"package={PKG}")
    print(f"key={KEY_PATH}（判完前勿开）")
    return 0


def write_docs(listen_order: list) -> None:
    read_lines = []
    for i, blk in enumerate(listen_order, 1):
        kind = "探索组（两问）" if blk.startswith("explore") else "正式块（5 轮强制选择）"
        read_lines.append(f"{i}. `{blk}/` —— {kind}")
    lines_block = "\n".join(read_lines)
    qblock = "\n".join(f"  - {q}" for q in EXPLORE_QUESTIONS)

    readme = f"""# 盲听文件包（票 #20；协议 = 票 #19 决议）

对比混音处理链路的听感。判定全部完成前，**不要打开仓库根的 `listen_package.key.json`**
（解盲钥匙），也不要去翻 `docs/spec/algo-registry.md` §6、`src/` 源码或 git 历史对号——
单盲靠约定，判完自然全知。

## 你会听到什么

每轮一个目录、目录里两个随机代号（三个字母）的 WAV；两段之中选**更接受**的一段。
同一块（目录）里 5 轮的音频相同，但**每轮代号与摆位重新随机**——逐轮独立判定，
即使与上一轮感觉相同也要重新选；**落笔不改**。

## 每轮判定引导语（判定时默念）

> {GUIDE}

## 试听顺序

{lines_block}

- 正式块（1–8）每块 5 轮，判定写进 `records.md` 对应行。
- 探索组（9–10）在正式块**全部判完后**再听，答两问 + 自由评语：
{qblock}
- 块间换耳休息 ≥10 分钟；可分多次/分天听完，不要求一气呵成。
- 播放：任意支持 WAV 的播放器，同轮两文件来回切即可；素材 44.1 kHz / 16-bit / 单声道，
  均经 `--match -18` 响度对齐（对齐后差异只反映失真与动态）。
- 素材与人声段署名见 `CREDITS.md`。

## 判完之后

`records.md` 填完 → 才可打开钥匙 `../listen_package.key.json` 解盲 → 找 agent 走票 #21
落登记表判定。

## 复现

本包由 `tools/make_listen_package.py` 以记录在钥匙里的 seed 生成；同 seed 重跑目录与
音频逐字节一致（工具为确定性 C 代码，无随机性）。
"""
    write_text(os.path.join(PKG, "README-listen.md"), readme)

    rec = ["# 判定记录表（票 #21 用；落笔不改）", ""]
    for i, blk in enumerate(listen_order, 1):
        rdir = os.path.join(PKG, blk)
        codes_here = dir_codes(rdir)
        if blk.startswith("explore"):
            rec += [f"## 第 {i} 听 · `{blk}/`（探索组）", "",
                    f"代号：`{codes_here[0]}` / `{codes_here[1]}`", ""]
            for q in EXPLORE_QUESTIONS:
                rec += [f"- {q}", ""]
            rec += ["- 自由评语：", ""]
        else:
            rec += [f"## 第 {i} 听 · `{blk}/`（5 轮）", ""]
            for r in range(1, N_ROUNDS + 1):
                rcodes = dir_codes(os.path.join(rdir, f"round{r}"))
                rec.append(f"- 轮 {r}（`{rcodes[0]}` vs `{rcodes[1]}`）：选 → ______")
            rec.append("")
    rec += ["---", "解盲后才填：每块选的是基准还是被测（由 agent 依钥匙解码）。"]
    write_text(os.path.join(PKG, "records.md"), "\n".join(rec))

    credits = """# 素材来源与署名（随包义务，票 #18/#20）

本包音频由仓库 `assets/` 两段素材经 `audio_mix`（`--match -18`）处理生成。

## 人声素材 `assets/cc_voice.wav` —— CC BY-SA 4.0

- 标题：Spoken Wikipedia — Eleventh Avenue (Manhattan)
- 朗读者 / 署名对象：JohnAnkerBow
- 来源：<https://commons.wikimedia.org/wiki/File:Spoken_Wikipedia_-_Eleventh_Avenue_(Manhattan).wav>
- 许可证：Creative Commons Attribution-Share Alike 4.0
  <https://creativecommons.org/licenses/by-sa/4.0/deed.zh>
- 修改说明：截取 48 s 段、立体声 L/R 均值转单声道（未施加增益或动态处理）。

## 音乐素材 `assets/cc_music.wav` —— 公有领域

- 曲目：Franz Schubert — Piano Sonata No. 13 in A major, D 664, II. Andante
- 演奏：Paul Pitman；录音由 Musopen 以公有领域发布
- 来源：<https://commons.wikimedia.org/wiki/File:Schubert_-_Piano_Sonata_No._13_in_A_major,_D664_-_II._Andante_(Paul_Pitman).flac>

分发本文件包时须随包携带本署名页；人声段衍生件按 CC BY-SA 4.0 相同方式共享。
"""
    write_text(os.path.join(PKG, "CREDITS.md"), credits)


if __name__ == "__main__":
    sys.exit(main())
