#!/bin/sh

LOGFILE="/home/stt/Smart-VR/logs/rem_old_result.log"
LOGFILE="rem_old_result.log"
OFFSET=30
WAVPATH="/home/boolpae/TEMP/WAVS/"
RT_STTPATH="/home/boolpae/TEMP/STT/REALTIME/"
FN_STTPATH="/home/boolpae/TEMP/STT/FILETIME/"

RMDIR='/bin/rm'
RMOPT='-rf'

RDATE=`date +"%Y%m%d" -d "${OFFSET} day ago"`
#echo $(date '+%Y%m%d')
#echo ${RDATE}

# remove directory
if [ -d ${WAVPATH}${RDATE} ]; then
	echo $(date) "- [START] remove directory(${WAVPATH}${RDATE})" >> ${LOGFILE}
	${RMDIR} ${RMOPT} ${WAVPATH}${RDATE}
fi
if [ -d ${RT_STTPATH}${RDATE} ]; then
	echo $(date) "- [START] remove directory(${RT_STTPATH}${RDATE})" >> ${LOGFILE}
	${RMDIR} ${RMOPT} ${RT_STTPATH}${RDATE}
fi
if [ -d ${FN_STTPATH}${RDATE} ]; then
	echo $(date) "- [START] remove directory(${FN_STTPATH}${RDATE})" >> ${LOGFILE}
	${RMDIR} ${RMOPT} ${FN_STTPATH}${RDATE}
fi

# write logs
if [ ! -d ${WAVPATH}${RDATE} ]; then
	echo $(date) "- [SUCCESS] removed directory(${WAVPATH}${RDATE})" >> ${LOGFILE}
else
	echo $(date) "- [FAILED] removed directory(${WAVPATH}${RDATE})" >> ${LOGFILE}
fi
if [ ! -d ${RT_STTPATH}${RDATE} ]; then
	echo $(date) "- [SUCCESS] removed directory(${RT_STTPATH}${RDATE})" >> ${LOGFILE}
else
	echo $(date) "- [FAILED] removed directory(${RT_STTPATH}${RDATE})" >> ${LOGFILE}
fi
if [ ! -d ${FN_STTPATH}${RDATE} ]; then
	echo $(date) "- [SUCCESS] removed directory(${FN_STTPATH}${RDATE})" >> ${LOGFILE}
else
	echo $(date) "- [FAILED] removed directory(${FN_STTPATH}${RDATE})" >> ${LOGFILE}
fi
