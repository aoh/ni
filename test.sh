#!/bin/bash

test -x /usr/bin/blab || { echo no blab; exit 0; }

test -d tmp && rm -rf tmp

mkdir tmp

for n in $(seq 0 6)
do
   echo -n "$n: "
   blab -e "97{$n}" > tmp/sample-$n
   for foo in $(seq 16)
   do
      $@ tmp/* > /dev/null || exit 1
      blab -e 42
   done
   blab -e 10
done

blab -ve 'O = S{1024,}  S = [a-z]+ | [0-9]+ | 32+ | "<" S ">" | "(" S ")"' -n 20 -o tmp/out-%n

echo -n "testing bigger samples: "
for n in $(seq 256)
do
   blab -e 42
   $@ tmp/* > tmp/ni-$n || exit 1
done

blab -e 10

echo "All OK"
