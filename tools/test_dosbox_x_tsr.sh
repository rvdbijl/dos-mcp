#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
watcom_root="${WATCOM:-/opt/watcom}"
dosbox_x="${DOSBOX_X:-dosbox-x}"
packet_driver="${PACKET_DRIVER:-}"
build_dir="$repo_root/dos/build"
test_dir="$(mktemp -d)"
dosbox_pid=""

cleanup() {
    if [[ -n "$dosbox_pid" ]] && kill -0 "$dosbox_pid" 2>/dev/null; then
        kill -KILL "$dosbox_pid" 2>/dev/null || true
        wait "$dosbox_pid" 2>/dev/null || true
    fi
    rm -rf -- "$test_dir"
}
trap cleanup EXIT INT TERM

make -C "$repo_root/dos" WATCOM="$watcom_root" all
cp -- "$repo_root/dos/tests/mtcp-dosbox.cfg" "$build_dir/MTCP.CFG"

if [[ -n "$packet_driver" ]]; then
    cp -- "$packet_driver" "$build_dir/NE2000.COM"
fi
if [[ ! -f "$build_dir/NE2000.COM" ]]; then
    echo "Set PACKET_DRIVER to a Crynwr-compatible NE2000.COM." >&2
    exit 2
fi
rm -f -- "$build_dir/PROTO.LOG" "$build_dir/CFG.LOG" "$build_dir/ZERO.LOG" \
    "$build_dir/ZERO-U.LOG" "$build_dir/TSR.LOG" "$build_dir/QUERY.LOG" \
    "$build_dir/UNLOAD.LOG"
rm -rf -- "$build_dir/REMOTE"

(
    cd -- "$build_dir"
    if [[ -n "${DOSBOX_LIBDIR:-}" ]]; then
        export LD_LIBRARY_PATH="$DOSBOX_LIBDIR"
    fi
    export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
    export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
    exec "$dosbox_x" \
        -conf "$repo_root/dos/dosbox-x-tsr.conf" \
        -silent -fastlaunch
) >"$test_dir/dosbox.log" 2>&1 &
dosbox_pid=$!

if ! UV_CACHE_DIR="${UV_CACHE_DIR:-/tmp/dos-mcp-uv-cache}" \
    uv run --offline python "$repo_root/tools/dosbox_tsr_e2e.py"; then
    echo "DOSBox-X log:" >&2
    tail -100 "$test_dir/dosbox.log" >&2
    echo "RA-TSR install log:" >&2
    [[ -f "$build_dir/TSR.LOG" ]] && cat "$build_dir/TSR.LOG" >&2
    echo "RA-TSR query log:" >&2
    [[ -f "$build_dir/QUERY.LOG" ]] && cat "$build_dir/QUERY.LOG" >&2
    exit 1
fi

for _ in {1..30}; do
    [[ -f "$build_dir/UNLOAD.LOG" ]] && break
    sleep 0.1
done
grep -q "RA-TSR unloaded" "$build_dir/UNLOAD.LOG"
grep -q "PASS protocol vectors" "$build_dir/PROTO.LOG"
grep -q "PASS mTCP configuration vectors" "$build_dir/CFG.LOG"
grep -Fq 'using MTCPCFG C:\MTCP.CFG' "$build_dir/TSR.LOG"
grep -Fq '"DOSBOX-TSR" installed at 10.0.2.15:21300' \
    "$build_dir/ZERO.LOG"
grep -q "RA-TSR unloaded" "$build_dir/ZERO-U.LOG"
