#! /bin/bash

_utilsDir=$(cd $(dirname $0);pwd)

_statusbarDir=$(cd $(dirname $0);cd ../statusbar;pwd)

# 任务分析
$_utilsDir/task_todo.sh analysis &