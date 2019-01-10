#!/bin/bash

PROCESS=$1
#PID=`/bin/pidof $PROCESS`
#PID=`ps cax | grep $PROCESS | grep -o '^[ ]*[0-9]*'`
SLEEP='/bin/sleep'
PATH='/home/boolpae/Dev/CodeExam/ShellScripts'

while [ true ];
do
        PID=`/bin/pidof $PROCESS`
        if [ -z "${PID}" ]; then
                CMD=${PATH}/${PROCESS}
                echo "${PROCESS} is not exist. Run ${CMD}"
                $CMD
        else
                echo "${PROCESS} is running...PID(${PID})"
        fi
        $SLEEP 1;
done
