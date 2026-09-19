#!/bin/bash
# DATE 获取日期和时间的脚本

todo_sh=$(cd $(dirname $0);cd ..;pwd)/utils/task/task_todo.sh

TODAY=$(date +%Y%m%d)

notify() {
    local d1 d2 d3 month_title today_day cal_rows cal_markup
    local open_tasks all_count near_count today_count task_lines stats body
    d1="D:$(date '+%Y-%m-%d')"
    d2="D:$(date -d '-1 day ago' '+%Y-%m-%d')"
    d3="D:$(date -d '-2 day ago' '+%Y-%m-%d')"

    # Keep the calendar aligned with a monospace span. util-linux cal supplies
    # Monday-first rows; the header is deliberately Chinese as requested.
    month_title=$(date '+%Y年%m月')
    today_day=$(date '+%-d')
    cal_rows=$(LC_ALL=C cal -m 2>/dev/null | tail -n +3)
    cal_rows=$(printf '%s\n' "$cal_rows" | sed -E \
      "s/(^| )(${today_day})( |$)/\\1<span foreground=\"#F38BA8\"><b>\\2<\\/b><\\/span>\\3/")
    printf -v cal_markup '<tt>     %s\n一 二 三 四 五 六 日\n%s</tt>' \
      "$month_title" "$cal_rows"

    open_tasks=$(grep -h -- '- \[ \] ' "$HOME"/work/todo/todo_*.md 2>/dev/null |
      grep -v -- '- \[ \][[:space:]]*S' || true)
    all_count=$(printf '%s\n' "$open_tasks" | grep -c .)
    near_count=$(printf '%s\n' "$open_tasks" | grep -c "$d2\|$d3")
    today_count=$(printf '%s\n' "$open_tasks" | grep -c "$d1")

    task_lines=$(printf '%s\n' "$open_tasks" | grep "$d1" |
      sed 's/- \[ \] /• /;s/[SD]:.*//' | head -5)
    if [[ -z "$task_lines" ]]; then
      task_lines=$(printf '%s\n' "$open_tasks" | grep -v "$d2\|$d3" |
        sed 's/- \[ \] /• /;s/[SD]:.*//' | head -5)
    fi
    task_lines=$(printf '%s' "$task_lines" |
      sed 's/&/\&amp;/g;s/</\&lt;/g;s/>/\&gt;/g')

    stats="<b><span color=\"#A6E3A1\">Tasks ${all_count}</span></b>  <b><span color=\"#F9E2AF\">Due ${near_count}</span></b>  <b><span color=\"#F38BA8\">Today ${today_count}</span></b>"
    printf -v body '%s\n\n%s' "$cal_markup" "$stats"
    if [[ -n "$task_lines" ]]; then
      printf -v body '%s\n\n<span color="#CDD6F4">%s</span>' \
        "$body" "$task_lines"
    fi

    notify-send "  Calendar" "$body" -r 9527
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

    t1="<b><span color=\"#A6E3A1\">任务:$_all</span></b>"
    t2="<b><span color=\"#F9E2AF\">临期:$_near3day</span></b>"
    t3="<b><span color=\"#F38BA8\">今日:$_today</span></b>"
    _todotext="$t1 $t2 $t3\n"

    [ "$_todaytask" ] && _todaytext="<b><span color=\"#F38BA8\">\n$_todaytask</span></b>"
    [ "$_near3daytask" ] && _near3daytext="<b><span color=\"#F9E2AF\">\n$_near3daytask</span></b>"
    [ ! "$_todaytask" ] && [ ! "$_near3daytask" ] && _todaytext="<b><span color=\"#F38BA8\">\n$_overduetask</span></b><b><span color=\"#89B4FA\">\n\n$_futuretask</span></b>"

    notify-send "  TodoList" "$_todotext$_todaytext$_near3daytext" -r 9527
}

task_overdue() {
    # 过期任务数
    _overdueday=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | awk -F'D:' -v today="$(date +%F)" '{if ($2 < today) print $0}' | wc -l)
    # 过期任务
    _overduetask=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | awk -F'D:' -v today="$(date +%F)" '{if ($2 < today) print $0}' | sed 's/- \[ \] //' | sed 's/[SD]:.*//')

    t1="<b><span color=\"#F38BA8\">过期任务:$_overdueday</span></b>"
    _overduetext="<b><span color=\"#F38BA8\">\n$_overduetask</span></b>"
    notify-send "  OverdueList" "$t1\n$_overduetext" -r 9527
}

task_future() {
    # 计划任务数
    _futureday=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | awk -F'S:' -v today="$(date +%F)" '{d=substr($2,1,10); if (d > today) print $0}' | wc -l)
    # 计划任务
    _futuretask=$(cat ~/work/todo/todo_*.md | grep "\- \[ \]" | grep -v "\- \[ \]\sS" | awk -F'S:' -v today="$(date +%F)" '{d=substr($2,1,10); if (d > today) print $0}' | sed 's/- \[ \] //' | sed 's/[SD]:.*//')

    t1="<b><span color=\"#89B4FA\">计划任务:$_futureday</span></b>"
    _futuretext="<b><span color=\"#89B4FA\">\n$_futuretask</span></b>"

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