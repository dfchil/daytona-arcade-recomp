#!/usr/bin/env sh
# Fetch SH4ZAM (SH4 math library) at the pinned commit into extern/sh4zam (git-ignored) and build its static
# library there with KOS's toolchain (environ.sh; extern/sh4zam/lib/libsh4zam.a), so the build does not depend on the copy installed in KOS's addons.
# v0.9.0 (0fd3a1e) is the release the renderer was written and measured with; 2d550a1 (installed in KOS on
# 18 October 2026) turns shz_vec4_t's e[4] into a vector type, which geometry.c's pointers into it do not accept.
set -eu
SH4ZAM_COMMIT=0fd3a1e1fa0809d33198c062632b1494ec2f57df
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
DEST="$ROOT/extern/sh4zam"
[ -d "$DEST/.git" ] || git clone https://github.com/gyrovorbis/sh4zam.git "$DEST"
git -C "$DEST" fetch --depth 1 origin "$SH4ZAM_COMMIT" 2>/dev/null || true
git -C "$DEST" checkout --detach "$SH4ZAM_COMMIT"
kos-cmake -S "$DEST" -B "$DEST/build" -DSHZ_ENABLE_TESTS=off -DSHZ_SAVE_TEMPS=off >/dev/null
cmake --build "$DEST/build" --target sh4zam
mkdir -p "$DEST/lib"
cp "$DEST/build/libsh4zam.a" "$DEST/lib/libsh4zam.a"
