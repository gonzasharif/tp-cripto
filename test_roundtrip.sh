#!/usr/bin/env bash
#
# test_roundtrip.sh — distribute + recover sanity check for visualSSS.
#
# For each k it distributes the secret into n=k shadows and recovers it,
# checking that the output keeps the secret's dimensions and measuring the
# distortion (the recovered image is not bit-identical: the GF(257) overflow
# handling is inherently lossy, see INFORME §2). It also verifies the
# threshold property: with fewer than k shadows recovery must refuse.
#
# Usage: ./test_roundtrip.sh
# Requires: a built ./visualSSS (the script builds it) and python3.

set -u
cd "$(dirname "$0")"

BIN=./visualSSS
SRC=archivosdeprueba
SECRET_SRC="$SRC/Marilynssd.bmp"
KS="2 3 4 8 10"

command -v python3 >/dev/null 2>&1 || { echo "python3 is required"; exit 1; }

echo "=== building ==="
make >/dev/null || { echo "build failed"; exit 1; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Pixel-body comparison: prints the dimensions of the recovered image and the
# fraction of pixels that differ from the original (header/palette excluded).
compare() {
  python3 - "$1" "$2" <<'PY'
import sys, struct
def info(p):
    d = open(p, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w   = struct.unpack('<i', d[18:22])[0]
    h   = struct.unpack('<i', d[22:26])[0]
    return d, off, w, h
o, oo, ow, oh = info(sys.argv[1])
r, ro, rw, rh = info(sys.argv[2])
n   = min(len(o), len(r))
bad = sum(1 for i in range(oo, n) if o[i] != r[i])
dims = "OK" if (rw == ow and rh == oh) else "DIMS MISMATCH (%dx%d)" % (rw, rh)
print("recovered=%dx%d %s  distortion=%.3f%%" % (rw, rh, dims, 100.0 * bad / (ow * oh)))
PY
}

pass=0; fail=0

echo "=== roundtrips (secret kept out of the carrier directory) ==="
for k in $KS; do
  sec="$WORK/sec"; car="$WORK/car"; out="$WORK/out"
  rm -rf "$sec" "$car" "$out"; mkdir -p "$sec" "$car" "$out"
  cp "$SECRET_SRC" "$sec/secret.bmp"

  # Build a pool of k carriers from the other test images, duplicating if k
  # exceeds the number of distinct images available.
  i=0
  for f in "$SRC"/*.bmp; do
    [ "$(basename "$f")" = "$(basename "$SECRET_SRC")" ] && continue
    cp "$f" "$car/c$i.bmp"; i=$((i+1))
    [ "$i" -ge "$k" ] && break
  done
  while [ "$i" -lt "$k" ]; do
    cp "$car/c0.bmp" "$car/c$i.bmp"; i=$((i+1))
  done

  if ! "$BIN" -d -secret "$sec/secret.bmp" -k "$k" -n "$k" -dir "$car" >"$WORK/d.log" 2>&1; then
    echo "k=$k  DISTRIBUTE FAILED"; tail -2 "$WORK/d.log"; fail=$((fail+1)); continue
  fi
  if ! "$BIN" -r -secret "$out/rec.bmp" -k "$k" -dir "$car" >"$WORK/r.log" 2>&1; then
    echo "k=$k  RECOVER FAILED"; tail -2 "$WORK/r.log"; fail=$((fail+1)); continue
  fi
  printf "k=%-2s  %s\n" "$k" "$(compare "$sec/secret.bmp" "$out/rec.bmp")"
  pass=$((pass+1))
done

echo
echo "=== threshold: k-1 shadows must be refused (k=4, keep 3) ==="
sec="$WORK/sec"; car="$WORK/car2"; out="$WORK/out2"
rm -rf "$car" "$out"; mkdir -p "$car" "$out"
cp "$SECRET_SRC" "$sec/secret.bmp"
i=0
for f in "$SRC"/*.bmp; do
  [ "$(basename "$f")" = "$(basename "$SECRET_SRC")" ] && continue
  cp "$f" "$car/c$i.bmp"; i=$((i+1)); [ "$i" -ge 4 ] && break
done
"$BIN" -d -secret "$sec/secret.bmp" -k 4 -n 4 -dir "$car" >/dev/null 2>&1
rm -f "$car/c3.bmp"   # leave only 3 shadows
if "$BIN" -r -secret "$out/rec.bmp" -k 4 -dir "$car" >"$WORK/t.log" 2>&1; then
  echo "FAILED: recovery succeeded with only 3 shadows"; fail=$((fail+1))
else
  echo "refused as expected: $(grep -iE 'error|need' "$WORK/t.log" | head -1)"
  pass=$((pass+1))
fi

echo
echo "=== summary: PASS=$pass FAIL=$fail ==="
[ "$fail" -eq 0 ]
