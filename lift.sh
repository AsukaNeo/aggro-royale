#!/bin/bash
# usage: ./lift.sh [run.log]  -- (seed from a run's log,) lift the XBE to C, then build
set -eo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
XBE="$ROOT/game/default.xbe"
SECTIONS=D3DX,XGRPH,DSOUND,WMADECXM,WMADEC,BINK,BINK32,BINK32A,BINK16,BINK4444,BINK5551,BINKYUY2,BINK16MX,BINK16X2,BINK16M,BINK32MX,BINK32X2,BINK32M,D3D,XPP,DOLBY
cd "$ROOT/../xboxrecomp"
AJ=tools/disasm/output/default_analysis.json
mkdir -p tools/disasm/output
[ -f $AJ ] || py -3 -m tools.xbe_parser "$XBE" --json $AJ --quiet
if [ -n "$1" ]; then
  py -3 -m tools.seed_from_log "$ROOT/$1" "$XBE" --functions tools/disasm/output/functions.json --seeds "$ROOT/seeds/seed_functions.json" --analysis-json $AJ
fi
py -3 -m tools.disasm "$XBE" --analysis-json $AJ --text-only --extra-sections $SECTIONS --seed-functions "$ROOT/seeds/seed_functions.json" | tail -5
py -3 -m tools.func_id "$XBE" | tail -3
py -3 -m tools.abi_analysis "$XBE" | tail -3
py -3 -m tools.recomp "$XBE" --all --split 250 --back-edge-yield --spin-wait 0x00177573 --spin-wait 0x0017CEEB --spin-wait 0x0017D24A --spin-wait 0x0017F0A9 --gen-dir "$ROOT/src/recomp/gen" --exclude-manual "$ROOT/src/recomp_manual.c" 2>&1 | tail -8
