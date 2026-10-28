# PlatformIO pre-build check: the FreeInk SDK submodule still carries the panel
# behaviour this launcher depends on.
#
# src/sdk_requirements.h checks the SDK's feature macros at compile time. This
# checks the two things a macro cannot vouch for, because an upstream merge can
# keep the marker and still lose the behaviour:
#
#   - aurora's clean bank: every generated kCleanR table is exactly
#     2 * kDriveFrames rows (black, then white, greys landed from white).
#   - the clean refresh normalizes unconditionally. Window refreshes leave
#     pixels the clean bank would skip, so a normalize gated on "the last push
#     was a clean one" leaves grey residue after every drag.
#
# Run it by hand with:  python3 scripts/check_sdk.py
# See freeink-sdk/docs/launcher-features.md.

import os
import re
import sys

try:
    Import("env")  # noqa: F821  (PlatformIO injects this)
    ROOT = env.subst("$PROJECT_DIR")  # noqa: F821
except NameError:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SDK = os.path.join(ROOT, "freeink-sdk")
WAVEFORM = os.path.join(SDK, "libs/hardware/BoardT5S3/src/ED047TC2Waveform.cpp")
DRIVER = os.path.join(SDK, "libs/display/FreeInkDisplay/src/driver/LgfxEpdDriver.cpp")


def read(path):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read()
    except OSError:
        return None


def check():
    problems = []
    wave = read(WAVEFORM)
    drv = read(DRIVER)
    if wave is None or drv is None:
        return ["freeink-sdk is missing or incomplete; run: git submodule update --init"]

    m = re.search(r"kDriveFrames\[kTempRangeCount\]\s*=\s*\{([^}]*)\}", wave)
    tables = dict(
        (int(i), body.count("LUT_MAKE("))
        for i, body in re.findall(r"constexpr uint32_t kCleanR(\d+)\[\] = \{(.*?)\n\};", wave, re.S)
    )
    if not m or not tables:
        problems.append("ED047TC2Waveform.cpp no longer has kDriveFrames / kCleanR tables to check")
    else:
        frames = [int(x) for x in m.group(1).split(",") if x.strip()]
        for i, l15 in enumerate(frames):
            rows = tables.get(i)
            if rows != 2 * l15:
                problems.append(
                    f"clean bank range {i} is {rows} frames, expected 2*L15 = {2 * l15} "
                    "(aurora's 2*L15 clean refresh was replaced)"
                )

    body = re.search(r"void normalizeForCleanBank\(\) \{(.*?)\n\}", drv, re.S)
    if not body:
        problems.append("LgfxEpdDriver.cpp no longer has normalizeForCleanBank()")
    elif "lastPushUsedCleanBank" in body.group(1):
        problems.append(
            "normalizeForCleanBank() is gated on the last push again; clean refreshes "
            "would leave window-refresh residue on the glass"
        )
    return problems


problems = check()
if problems:
    sys.stderr.write("\nfreeink-sdk check failed (see freeink-sdk/docs/launcher-features.md):\n")
    for p in problems:
        sys.stderr.write(f"  - {p}\n")
    sys.stderr.write("\n")
    if "env" in globals():
        env.Exit(1)  # noqa: F821
    sys.exit(1)
elif "env" not in globals():
    print("freeink-sdk check passed")
