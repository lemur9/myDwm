#!/bin/zsh

# 获取当前日期（格式：YYYYMMDD）
TODAY=$(date +%Y%m%d)

# 获取当前日期（格式：YYYY-MM-DD）
TASK_TIME=$(date +%Y-%m-%d)

# 定义文件路径（默认用户目录，可自定义）
TODO_FILE="$HOME/work/todo/todo_${TODAY}.md"

create_task() {
    # 若文件不存在则创建
    if [ ! -f "$TODO_FILE" ]; then
        touch "$TODO_FILE"
        echo "# ${TASK_TIME} 待办清单" > "$TODO_FILE"
        echo "- [ ] S:${TASK_TIME} D:${TASK_TIME}" >> "$TODO_FILE"
    else
        nvim --noplugin -c "set autowriteall" -c "set updatetime=3000" -c "autocmd CursorHold,CursorHoldI * silent wa" "$TODO_FILE"
    fi
}

analysis() {
    [ ! -f "$TODO_FILE" ] && create_task

    for file in ~/work/todo/todo_*.md; do
        if [ "$file" = "$TODO_FILE" ]; then continue; fi

        _expired=$(grep "\- \[ \]" "$file" | grep -v "\- \[ \]\sS" | awk -v today="$TASK_TIME" '{match($0,/D:([0-9-]+)/,a);if (a[1] < today) print $0;}')

        _future=$(grep "\- \[ \]" "$file" | grep -v "\- \[ \]\sS" | awk -v today="$TASK_TIME" '{match($0,/S:([0-9-]+)/,a);if (a[1] >= today) print $0;}')

        if [ -n "$_expired" ]; then
            printf '%s\n' "$_expired" | sed -i '1r /dev/stdin' "$TODO_FILE"
            printf '%s\n' "$_expired" | grep -F -x -v -f - "$file" > tmp && mv tmp "$file"
        fi

        if [ -n "$_future" ]; then
            echo "$_future" >> "$TODO_FILE"
            printf '%s\n' "$_future" | grep -F -x -v -f - "$file" > tmp && mv tmp "$file"
        fi
    done
}

clear_task() {
    # 清除超过7天的历史任务
    find ~/work/todo -name "todo_*.md" -type f -mtime +7 -delete 2>/dev/null
}

case $1 in
    create) create_task ;;
    analysis) clear_task && analysis ;;
    *) clear_task ;;
esac