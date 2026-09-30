import os

Import("env")

# The hook keeps its double-press flag in the sector at 0x7000.
ARM_FLAG_ADDR = 0x7000


def check_size(source, target, env):
    size = os.path.getsize(env.subst("$BUILD_DIR/bootloader.bin"))
    if size > ARM_FLAG_ADDR:
        raise SystemExit(f"bootloader.bin is {size} bytes and would overlap the flag sector at 0x7000")


# `pio run -t flashbl`: write only the bootloader at 0x0. The partition table,
# otadata, Basilauncher and guest slots are left untouched.
env.AddCustomTarget(
    name="flashbl",
    dependencies="$BUILD_DIR/bootloader.bin",
    actions=[
        check_size,
        '"$PYTHONEXE" -m esptool --chip esp32s3 --port "$UPLOAD_PORT" --baud 921600 '
        "write-flash 0x0 $BUILD_DIR/bootloader.bin"
    ],
    title="Flash bootloader",
    description="Write custom bootloader at 0x0",
)
