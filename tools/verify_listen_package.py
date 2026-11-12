#!/usr/bin/env python3
# 盲听包完整性核验（票 #20 验收工具）。
# 用法：python3 tools/verify_listen_package.py <listen_package.key.json>
# 自己重跑 audio_mix 得到链路哈希表（工具确定性），对照钥匙逐一核对包内代号；
# 输出只含检查数与 PASS/FAIL，不打印任何代号→链路映射。

import hashlib
import json
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PKG = os.path.join(REPO, "listen_package")


def sha256(path):
    h = hashlib.sha256()
    try:
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
    except OSError as e:
        sys.exit(f"error: cannot read {path}: {e}")
    return h.hexdigest()


def check(cond, what, fails, counter):
    counter[0] += 1
    if not cond:
        fails.append(what)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: verify_listen_package.py <key.json>")
    try:
        with open(sys.argv[1], encoding="utf-8") as f:
            key = json.load(f)
    except (OSError, ValueError) as e:
        sys.exit(f"error: cannot load key: {e}")

    fails = []
    counter = [0]

    # 重跑工具得链路哈希表（确定性，见票 #20 复现说明）
    chain_files = key["chain_files"]
    build = {}
    for m in key["materials"]:
        src = os.path.join(REPO, key["materials"][m])
        try:
            outdir = tempfile.mkdtemp(prefix=f"pkg_verify_{m}_")
            subprocess.run(["./audio_mix", src, src, "--match", "-18", "--outdir", outdir],
                           cwd=REPO, check=True)
        except (OSError, subprocess.CalledProcessError) as e:
            sys.exit(f"error: audio_mix rebuild failed for {m}: {e}")
        build[m] = {c: sha256(os.path.join(outdir, f)) for c, f in chain_files.items()}

    seen_basis = {}
    for rk in key["rounds"]:
        rdir = os.path.join(PKG, rk["block"], f"round{rk['round']}")
        fb = os.path.join(rdir, rk["basis_code"] + ".wav")
        fv = os.path.join(rdir, rk["variant_code"] + ".wav")
        m = "voice" if rk["block"].endswith("voice") else "music"
        check(sha256(fb) == build[m]["basis"], f"basis->01 {rdir}", fails, counter)
        variant = key["groups"][rk["block"].split("_")[0]]["variant"]
        check(sha256(fv) == build[m][variant], f"variant {rdir}", fails, counter)
        check(sha256(fv) != sha256(fb), f"distinct {rdir}", fails, counter)
        seen_basis.setdefault(m, set()).add(sha256(fb))

    for m, hs in seen_basis.items():
        check(len(hs) == 1, f"basis-consistency {m}", fails, counter)

    for ek in key["explore"]:
        d = os.path.join(PKG, ek["dir"])
        m = ek["material"]
        check(sha256(os.path.join(d, ek["float_code"] + ".wav")) == build[m]["float"],
              f"explore-float {d}", fails, counter)
        check(sha256(os.path.join(d, ek["truepeak_code"] + ".wav")) == build[m]["truepeak"],
              f"explore-tp {d}", fails, counter)

    for blk in {rk["block"] for rk in key["rounds"]}:
        bases = {rk["basis_code"] for rk in key["rounds"] if rk["block"] == blk}
        check(len(bases) >= 2, f"position-rotation {blk}", fails, counter)

    print(f"checks={counter[0]} fails={len(fails)}")
    print("BLIND-PACKAGE VERIFY: " + ("PASS" if not fails else "FAIL " + "; ".join(fails[:5])))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
