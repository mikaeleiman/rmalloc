#!/bin/bash

#set -x

if [[ "$1" == "" || ! -f "$1" ]]; then
echo "opsfile req'd"
	exit
fi


CORES=$(grep -c ^processor /proc/cpuinfo)
let CORES=2*$CORES
export DATAPOINTS=1500 # requested -- will be adjusted down if neccessary.

export opsfile=$1
export RESULTFILE=$(basename $opsfile)-allocstats

echo -n "Calculating peak mem... "
# XXX: re-enable this
#peakmem=50485760
export peakmem=$(./plot_dlmalloc --peakmem $opsfile 2> /dev/null)
export theory_peakmem=$peakmem

#peakmem=$(echo "$peakmem*1.05/1" | bc)
#echo "$theory_peakmem bytes. Increased by 5% => $peakmem bytes"
echo "$theory_peakmem bytes"

# alright, try to figure out how small we can make peakmem while still not getting OOMs.
# this could take some time...

# done=0
# count=$(wc -l $opsfile | awk '{print $1}')
# while [[ "$done" != "1" ]]; do
#     for i in $(seq 0 $count); do
#         ./plot_dlmalloc --maxmem $opsfile $i $peakmem $theory_peakmem > /dev/null 2>&1
#         status=$?
#         if [[ "$status" == "2" ]]; then
#             # oom, bump by 5% and retry.
#             peakmem=$(echo "$peakmem*1.05/1" | bc)
#             echo
#             echo "OOM! Bump by 5% up to $peakmem bytes\n"
#             break
#         fi
#         echo -ne "\r                               \r$i / $count "
#         #echo -n "."
#         #echo "./plot_dlmalloc --maxmem $opsfile $i (of $count) $peakmem (of theoretical $theory_peakmem) => exit code $status"
#     done
    # if we've reached this point, we didn't have an OOM. yay.
#     # proceed.
#     break
# done
# # delete file after. it is no longer of interest
# echo > dlmalloc.alloc-stats

done=0
fullcount=$(wc -l $opsfile | awk '{print $1}')


echo "fullcount = $fullcount"

export OPS_COUNT=$(grep '\(N\|F\)' $opsfile | wc -l | awk '{print $1}')

echo "ops_count (N/F ops) = $OPS_COUNT"

while [[ "$done" != "1" ]]; do
    echo "calculating maxmem for $peakmem"

    # XXX: re-enable this later.

    #echo ./plot_dlmalloc --maxmem $opsfile $fullcount $peakmem $theory_peakmem
    ./plot_dlmalloc --maxmem $opsfile $RESULTFILE $fullcount $peakmem $theory_peakmem > /dev/null 2>&1
    status=$?
    #if [[ "$status" == "2" ]]; then # don't test for 2, in case of crash - test for 0.
    if [[ "$status" != "0" ]]; then
        # oom, bump by 5% and retry.
        peakmem=$(echo "$peakmem*1.05/1" | bc)
        echo
        echo "OOM! Bump by 5% up to $peakmem bytes\n"
    else
        break
        #echo -n "."
        #echo "./plot_dlmalloc --maxmem $opsfile $i (of $count) $peakmem (of theoretical $theory_peakmem) => exit code $status"
    fi
done

echo > $RESULTFILE

# each run "discards" the previous ones, i.e. doesn't try to do max-size allocs for any but the last

if [[ "$DATAPOINTS" -gt "$OPS_COUNT" ]]; then
    DATAPOINTS=$OPS_COUNT
fi
ENDPOINTS=$DATAPOINTS

declare -a corejobs

i=0
for start in $(seq 0 10 $ENDPOINTS); do
    s="${corejobs[$i]} $start"
    corejobs[$i]=$s
    let i=i+1
    if [[ "$i" == "$CORES" ]]; then
        i=0
    fi
done

echo "Starting $CORES jobs."

rm -rf donefile.*

let jobs=$CORES-1

for i in $(seq 0 $jobs); do
    ./run_maxmem_payload.sh donefile.$i ${corejobs[$i]} &
done

echo "Waiting for run to finish."

continue=1
while [[ "$continue" == "1" ]]; do
    sleep 1s
    continue=0
    for i in $(seq 0 $jobs); do
        if [[ ! -f "donefile.$i" ]]; then
            #echo "* $i not yet done."
            continue=1
        else
            echo "* $i done"
        fi
    done
done

rm -rf donefile.*

#ls -1 ${RESULTFILE}.part* | sort -n | xargs ls # cat >> $RESULTFILE

cat ${RESULTFILE}.part* >> $RESULTFILE
echo ']' >> $RESULTFILE

rm -rf ${RESULTFILE}.part*


# 
# # number of operations to perform..
# # each run "discards" the previous ones, i.e. doesn't try to do max-size allocs for any but the last
# echo "./plot_dlmalloc --maxmem $opsfile $i $peakmem $theory_peakmem (of $count)"
# i=0
# while [[ "$i" != "$count" ]]; do
# #for i in $(seq 0 $count); do
#     echo -ne "\r                               \r$i / $count "
#     ./plot_dlmalloc --maxmem $opsfile $i $peakmem $theory_peakmem > /dev/null 2>&1
# 
#     let i=$i+1
# done
# echo ']' >> dlmalloc.alloc-stats

#python grapher.py dlmalloc.maxmem-stats
python grapher.py $RESULTFILE
