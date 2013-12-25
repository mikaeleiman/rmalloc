#!/bin/bash

if [[ "$1" == "" || ! -f "$1" ]]; then
echo "opsfile req'd"
	exit
fi

if [[ "$ALLOCATOR" == "" || ! -f "$ALLOCATOR" ]]; then
    echo "ALLOCATOR not set or does not exist. Set it to the path to test program, e.g. ALLOCATOR=./plot_dlmalloc"
    exit
fi

OPSFILE=$1

echo "* Generating plots."
$ALLOCATOR --fragmentation $OPSFILE

ANIMATION=${OPSFILE}-animation.avi

rm -rf "${ANIMATION}"

echo "* Producing animation AVI: $ANIMATION"
ffmpeg -f image2 -r 20 -i ${OPSFILE}-plot-%6d.png -r 30  -vcodec mjpeg -sameq ${ANIMATION}

rm -rf ${OPSFILE}-plot*png

