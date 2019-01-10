#!/bin/sh

GEAR_PATH=""
STAS_PATH="/home/boolpae/Dev/stas"
GEARMAN="/usr/local/sbin/gearmand"
STAS="/home/boolpae/Dev/stas/bin/Debug/Linux_x86_64/itf_stas_odbc"
#G_PID="/home/boolpae/Dev/stas/config/gearmand.pid"
G_PID="/home/boolpae/gearmand.pid"
S_PID="/home/boolpae/Dev/stas/config/itf_stas.pid"
G_OPT="--pid-file=${G_PID} -d"
S_OPT="-i /home/boolpae/Dev/stas/config/stas.conf --verbose DEBUG --pid-file=${S_PID} -d"

KILL="/bin/kill"
KILL_OPT="-9"

export ODBCINI=/etc/odbc.ini
export ODBCSYSINI=/etc
export ORACLE_HOME=/u01/app/oracle/instantclient_12_2
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/u01/app/oracle/instantclient_12_2
export TNS_ADMIN=/etc/oracle
export NLS_LANG=American_America.AL32UTF8


echo "## Gearman Exec [Args] ##"
echo ${GEARMAN} ${G_OPT}
echo
echo "## ITF_STAS Exec [Args] ##"
echo ${STAS} ${S_OPT}
echo

while true then
do

# GEARMAN
if [ -e ${G_PID} ]
then
	GPID=`cat ${G_PID}`
	if ps -p ${GPID} > /dev/null
	then
		echo "GEARMAND(${GPID}) is running" > /dev/null
	else
		echo "GEARMAND(${GPID}) is not running"
		echo 'Executing GEARMAN...'
		${GEARMAN} ${G_OPT}
	fi
else
	echo 'File not found('${G_PID}')'
	echo 'Executing GEARMAN...'
	${GEARMAN} ${G_OPT}
fi

# STT Tasks Allocation Service
if [ -e ${S_PID} ]
then
	SPID=`cat ${S_PID}`
	if ps -p ${SPID} > /dev/null
	then
		echo "STAS(${SPID}) is running" > /dev/null
	else
		echo "STAS(${SPID}) is not running"
		echo 'KILL Gearmand...'
		GPID=`cat ${G_PID}`
		${KILL} ${KILL_OPT} ${GPID}
		echo 'Executing STAS...'
		cd ${STAS_PATH}
		${STAS} ${S_OPT}
	fi
else
	echo 'File not found('${S_PID}')'
	echo 'Executing STAS...'
	cd ${STAS_PATH}
	${STAS} ${S_OPT}
fi

sleep 1

done
