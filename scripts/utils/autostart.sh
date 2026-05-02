#! /bin/bash

_utilsDir=$(cd $(dirname $0);pwd)

_statusbarDir=$(cd $(dirname $0);cd ../statusbar;pwd)

# 开始自动整理代办清单
$_utilsDir/task_todo.sh analysis &