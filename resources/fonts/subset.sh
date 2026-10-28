#!/bin/bash
set -e
cd "$(dirname "$0")"
for f in JetBrainsMono-Regular JetBrainsMono-Bold Inter-Regular Inter-Bold; do
  pyftsubset "$f.ttf" --output-file="$f-Subset.ttf" \
    --unicodes='U+0020-007E,U+00A0-00FF' --layout-features='' \
    --no-hinting --desubroutinize
  echo "$f ok"
done
ls -l *Subset.ttf
