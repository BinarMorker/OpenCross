#!/bin/sh
# Collect the enc_scale route's per-tick shots into tmp/ours_trans/frame_NNNNNN.png and write the measurement table (tmp/ours_<tag>.txt).
tag=${1:-now}
rm -rf tmp/ours_trans; mkdir -p tmp/ours_trans
for f in tests/routes/shots/es_*.png; do n=${f##*es_}; n=${n%.png}; cp "$f" tmp/ours_trans/frame_$(printf %06d $((10#$n))).png; done
CROP=0,8,1280,944 python tools/py/trans_measure.py tmp/ours_trans 1 > tmp/ours_$tag.txt
