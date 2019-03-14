#!/bin/sh

## Flushall at Redis
## 레디스가 운영 중인 서버에 cron으로 동작되도록 설정
echo flushall | redis-cli