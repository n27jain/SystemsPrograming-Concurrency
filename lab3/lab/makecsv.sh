#!/bin/bash
files=$(find . -name '*.dat')
for file in ${files}
do
 filename=$(basename "${file}" .dat)
 trimmed=$(echo ${filename} | sed 's|[BPCXN]||g')
 prepared="${trimmed//_/, }, "
 contents=$(cat ${file})
 avg=0.0
 n=0.0
 for line in ${contents}
 do
  avg=$(echo "$avg + $line" | bc)
  n=$(echo "$n + 1.0" | bc)
 done
 avg=$(echo "scale=2; $avg / $n" | bc)
 echo "${prepared}${avg}"
done
