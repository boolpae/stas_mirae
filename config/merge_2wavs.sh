#!/bin/bash

## ARG1($1) : FilePath of WAV File
## ARG2($2) : Call ID(wav file name)

DATE='date +%Y/%m/%d:%H:%M:%S'
# log file
LOG='/home/stt/Smart-VR/logs/merge_wavs.log'

function echo_log {
	echo `$DATE`" $1" >> $LOG
	if [ $2 != 0 ]; then
		exit $2
	fi
}

RM="/usr/bin/rm"
RM_USE="TRUE"
SOX="/usr/bin/sox"
#SOXI="/usr/bin/soxi"
SOX_OPTIONS="--multi-threaded -m"

ARG_CNT=$#

if [ $ARG_CNT -lt 2 ]; then
	#echo "Invalid Args"
	echo_log 'Failed(Invalid Args)' -1
fi

LWAV_FILE=$1"/"$2"_l.wav"
RWAV_FILE=$1"/"$2"_r.wav"
RES_FILE=$1"/"$2".wav"

## For debugging... output variables
#echo "PATH:    "$1
#echo "CALL-ID: "$2
#echo "ARG COUNT: "$ARG_CNT
#echo "Left   Wave: "$LWAV_FILE
#echo "Right  Wave: "$RWAV_FILE
#echo "Result Wave: "$RES_FILE

## For debugging... output command
#echo $SOX $SOX_OPTIONS $LWAV_FILE $RWAV_FILE $RES_FILE

$SOX $SOX_OPTIONS $LWAV_FILE $RWAV_FILE $RES_FILE && echo_log "SUCCESS - ${2}" 0 || echo_log "Failed to do sox - ${2}" 1

# remove files after success
if [ $RM_USE == "TRUE" ]; then
	$RM $LWAV_FILE $RWAV_FILE
fi
