#!/bin/bash
# Every screenshot the PX-BASIC manual uses, from the simulator, in
# build/shots: a/ the keyboard and a session, b/ errors and INPUT, c/ the
# examples, d/ the editor. Then the manual (basic_manual.html) and the
# README's keyword picture (docs-keywords.png).
#
#   tools/shots.sh
set -e
P=$(cd "$(dirname "$0")/.." && pwd)
S="$P/build/shots"
cd "$P"
make build/sim >/dev/null
rm -rf "$S"; mkdir -p "$S"
fresh() { rm -rf build/sim-drive && mkdir -p build/sim-drive && cp -R sim/drive/. build/sim-drive/; }
fresh
build/sim "$S/a" build/sim-drive shot:start ON shot:leave R2C4 R1C2 R1C3 shot:alpha R5C0 R2C0 shot:num R2C4 R2C4 R5C0 R2C0 R5C0 DOWN shot:kw1 DOWN shot:kw2 DOWN \
  serial:CLS serial:10_FOR_I=1_TO_5 'serial:20_PRINT_I,SQR(I)' serial:30_NEXT serial:RUN 100 shot:run \
  serial:CLS 'serial:LOAD_"GUESS"' serial:RUN serial:50 serial:25 serial:37 200 shot:guess RS \
  'serial:LOAD_"SPIRAL"' serial:RUN 4000 shot:spiral 'serial:LOAD_"SINE"' serial:RUN 4000 shot:sine ON >/dev/null
fresh
build/sim "$S/d" build/sim-drive serial:CLS serial:10_FOR_I=1_TO_5 100 'serial:20_PRINT_I,SQR(I)' 100 serial:30_NEXT 100 serial:CLS 100 UP UP shot:list ENTER shot:edit UP UP UP UP UP UP shot:cursor R5C0 R5C2 R5C0 R4C4 shot:inserted ENTER shot:accepted ON serial:RUN 100 shot:ran QUIT >/dev/null
fresh
build/sim "$S/b" build/sim-drive serial:CLS serial:PRINT_1/0 serial:PRIMT_1 serial:10_GOTO_99 serial:RUN 'serial:PRINT_SQR(-1)' 100 shot:errors \
  serial:NEW serial:CLS 'serial:10_INPUT_"RADIUS",R' 'serial:20_PRINT_"AREA_";PI*R^2' serial:30_GOTO_10 serial:RUN serial:2 serial:10 100 shot:input \
  RS 100 serial:LIST 100 shot:list ON >/dev/null
fresh
build/sim "$S/c" build/sim-drive serial:CLS 'serial:LOAD_"PRIMES"' serial:RUN 300 shot:primes serial:CLS 'serial:LOAD_"HELLO"' serial:RUN 300 shot:hello ON >/dev/null
magick "$S/a/kw1.png" -scale 200% "$P/docs-keywords.png"
python3 "$P/tools/make_manual.py"
