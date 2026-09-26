# Sourced by the bash tools: the Plan2001 target to work on.
#   TARGET  amd64 (default), arm64, riscv64: tools/targets/$TARGET
# Sets the target's variables (see tools/targets/amd64) and tbuild, the
# build output directory for it (build/$TARGET).  A target that is not
# supported yet is refused.
TARGET=${TARGET:-amd64}
tfile=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/targets/$TARGET
[ -f "$tfile" ] || { echo "target: no such target: $TARGET (see tools/targets)" >&2; exit 2; }
. "$tfile"
[ "$status" = supported ] || { echo "target: $TARGET is $status, not built yet (see tools/targets/$TARGET)" >&2; exit 2; }
tbuild=$(cd "$(dirname "$tfile")/../.." && pwd)/build/$TARGET
