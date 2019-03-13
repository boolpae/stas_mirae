#!/bin/sh

LOGFILE="/home/stt/Smart-VR/logs/rem_old_result.log"
LOGFILE="rem_old_result.log"
# 2달 - 60일
OFFSET=60
WAVPATH="/home/stt/Smart-VR/WAVS/"
RT_STTPATH="/home/stt/Smart-VR/STT/REALTIME/"
FN_STTPATH="/home/stt/Smart-VR/STT/FILETIME/"

RMDIR='/bin/rm'
RMOPT='-rf'

RDATE=`date +"%Y%m%d" -d "${OFFSET} day ago"`
#echo $(date '+%Y%m%d')
#echo ${RDATE}

# remove directory
echo $(date) "- [START] remove directory(${WAVPATH}${RDATE})" >> ${LOGFILE}
if [ -d ${WAVPATH}${RDATE} ]; then
	${RMDIR} ${RMOPT} ${WAVPATH}${RDATE}
fi
echo $(date) "- [START] remove directory(${RT_STTPATH}${RDATE})" >> ${LOGFILE}
if [ -d ${RT_STTPATH}${RDATE} ]; then
	${RMDIR} ${RMOPT} ${RT_STTPATH}${RDATE}
fi
echo $(date) "- [START] remove directory(${FN_STTPATH}${RDATE})" >> ${LOGFILE}
if [ -d ${FN_STTPATH}${RDATE} ]; then
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
