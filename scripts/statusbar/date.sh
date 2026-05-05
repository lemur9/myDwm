#!/bin/bash
# DATE 获取日期和时间的脚本

todo_sh=$(cd $(dirname $0);cd ..;pwd)/utils/task/task_todo.sh

TODAY=$(date +%Y%m%d)

notify() {
    d1="D:$(date '+%Y-%m-%d')"; d2="D:$(date -d '-1 day ago' '+%Y-%m-%d')"; d3="D:$(date -d '-2 day ago' '+%Y-%m-%d')"
    # 日历
    _cal=$(cal --color=always | sed 1,2d | sed 's/..7m/<b><span color="#FF79C6">/;s/..0m/<\/span><\/b>/')
    # 所有任务数
    _all=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | wc -l)
    # 临期任务数
    _near3day=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep "$d2\|$d3" | grep -v "\- \[ \]\sS" | wc -l)
    # 今日任务数
    _today=$(cat ~/work/todo/todo_$TODAY.md | grep "\- \[ \]" | grep "$d1" | grep -v "\- \[ \]\sS" | wc -l)

    # 今日任务
    _todaytask=$(cat ~/work/todo/todo_$TODAY.md | grep "\- \[ \]" | grep "$d1" | grep -v "\- \[ \]\sS" | sed 's/- \[ \] /- /' | sed 's/[SD]:.*//')
    # 兜底任务
    _fallbacktask=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | grep -v "$d2\|$d3" | sed 's/- \[ \] //' | sed 's/[SD]:.*//')

    t1="<b><span color=\"#54FF9F\">任务:$_all</span></b>"
    t2="<b><span color=\"#FFB90F\">临期:$_near3day</span></b>"
    t3="<b><span color=\"#FF79C6\">今日:$_today</span></b>"
    _todotext="$t1 $t2 $t3"

    [ "$_todaytask" ] && _todaytext="<b><span color=\"#FF79C6\">\n\n$_todaytask</span></b>"
    [ ! "$_todaytask" ] && _todaytext="<b><span color=\"#FFE0C8DD\">\n\n$_fallbacktask</span></b>"

    notify-send "  Calendar" "\n$_cal\n\n$_todotext$_todaytext" -r 9527
}

call_todo() {
    pid=`ps aux | grep 'st -t statusutil_todo' | grep -v grep | awk '{print $2}'`
    mx=`xdotool getmouselocation --shell | grep X= | sed 's/X=//'`
    my=`xdotool getmouselocation --shell | grep Y= | sed 's/Y=//'`
    dunstctl close 9527
    if [ -n "$pid" ] && kill $pid; then
        $todo_sh remind &
        $todo_sh auto_finish &
    else
        st -t statusutil_todo -g 50x15+$((mx))+$((my + 20)) -c float -e "$todo_sh" create
    fi
}

task_list() {
    d1="D:$(date '+%Y-%m-%d')"; d2="D:$(date -d '-1 day ago' '+%Y-%m-%d')"; d3="D:$(date -d '-2 day ago' '+%Y-%m-%d')"

    # 所有任务数
    _all=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | wc -l)
    # 临期任务数
    _near3day=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep "$d2\|$d3" | grep -v "\- \[ \]\sS" | wc -l)
    # 今日任务数
    _today=$(cat ~/work/todo/todo_$TODAY.md | grep "\- \[ \]" | grep "$d1" | grep -v "\- \[ \]\sS" | wc -l)

    # 今日任务
    _todaytask=$(cat ~/work/todo/todo_$TODAY.md | grep "\- \[ \]" | grep "$d1" | grep -v "\- \[ \]\sS" | sed 's/- \[ \] /- /' | sed 's/[SD]:.*//')
    # 临期任务
    _near3daytask=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | grep -v "$d1" | grep "$d2\|$d3" | sed 's/- \[ \] /- /' | sed 's/[SD]:.*//')
    # 过期任务
    _overduetask=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | awk -F'D:' -v today="$(date +%F)" '{if ($2 < today) print $0}' | sed 's/- \[ \] //' | sed 's/[SD]:.*//')
    # 计划任务
    _futuretask=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | awk -F'S:' -v today="$(date +%F)" '{d=substr($2,1,10); if (d > today) print $0}' | sed 's/- \[ \] //' | sed 's/[SD]:.*//')

    t1="<b><span color=\"#54FF9F\">任务:$_all</span></b>"
    t2="<b><span color=\"#FFB90F\">临期:$_near3day</span></b>"
    t3="<b><span color=\"#FF79C6\">今日:$_today</span></b>"
    _todotext="$t1 $t2 $t3\n"

    [ "$_todaytask" ] && _todaytext="<b><span color=\"#FF79C6\">\n$_todaytask</span></b>"
    [ "$_near3daytask" ] && _near3daytext="<b><span color=\"#FFB90F\">\n$_near3daytask</span></b>"
    [ ! "$_todaytask" ] && [ ! "$_near3daytask" ] && _todaytext="<b><span color=\"#FF3333\">\n$_overduetask</span></b><b><span color=\"#33CCFF\">\n\n$_futuretask</span></b>"

    notify-send "  TodoList" "$_todotext$_todaytext$_near3daytext" -r 9527
}

task_overdue() {
    # 过期任务数
    _overdueday=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | awk -F'D:' -v today="$(date +%F)" '{if ($2 < today) print $0}' | wc -l)
    # 过期任务
    _overduetask=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | awk -F'D:' -v today="$(date +%F)" '{if ($2 < today) print $0}' | sed 's/- \[ \] //' | sed 's/[SD]:.*//')

    t1="<b><span color=\"#FF3333\">过期任务:$_overdueday</span></b>"
    _overduetext="<b><span color=\"#FF3333\">\n$_overduetask</span></b>"
    notify-send "  OverdueList" "$t1\n$_overduetext" -r 9527
}

task_future() {
    # 计划任务数
    _futureday=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | awk -F'S:' -v today="$(date +%F)" '{d=substr($2,1,10); if (d > today) print $0}' | wc -l)
    # 计划任务
    _futuretask=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | awk -F'S:' -v today="$(date +%F)" '{d=substr($2,1,10); if (d > today) print $0}' | sed 's/- \[ \] //' | sed 's/[SD]:.*//')

    t1="<b><span color=\"#33CCFF\">计划任务:$_futureday</span></b>"
    _futuretext="<b><span color=\"#33CCFF\">\n$_futuretask</span></b>"

    notify-send "  FutureList" "$t1\n$_futuretext" -r 9527
}

case $BUTTON in
#case "$1" in
    1) notify ;;
    2) task_list ;;
    3) call_todo ;;
    4) task_overdue ;;
    5) task_future ;;
esac