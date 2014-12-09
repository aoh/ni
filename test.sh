#!/bin/bash

test -x /usr/bin/blab || { echo no blab; exit 0; }

test -d tmp && rm -rf tmp

set -e

mkdir tmp

echo "Testing $@"
echo "Using tiny samples: "
for n in $(seq 0 6)
do
   echo -n " - 0-${n}b: "
   blab -e "97{$n}" > tmp/sample-$n
   for foo in $(seq 8)
   do
      $@ tmp/* > /dev/null || exit 1
   done
   blab -e 10
done

echo "Generating sample files"
blab -e 'O = S{1024,}  S = [a-z]+ | [0-9]+ | 32+ | "<" S ">" | "(" S ")"' -n 10 -o tmp/out-%n

echo -n "Generating 256 testcases from bigger files to tmp/ni-%n:"
$@ -o tmp/ni-%n -n 256 tmp/* || exit 1
echo " ok"

SEED=$RANDOM$RANDOM$RANDOM
NSAMPLES=$(find . -type f | wc -l)
echo -n "Generating 10K testcases with seed $SEED using $NSAMPLES files from $(pwd): "
echo "$($@ -s $SEED -n 10000 $(find . -type f) | wc -c) bytes generated"

echo "Everything seems to be in order."
