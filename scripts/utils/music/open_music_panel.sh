#!/bin/bash

# ==============================
# 自动计算 st 尺寸
# ==============================

BAR_H=32
GAP=8

# 获取屏幕尺寸
SCREEN_W=$(xdpyinfo | awk '/dimensions/{print $2}' | cut -d'x' -f1)
SCREEN_H=$(xdpyinfo | awk '/dimensions/{print $2}' | cut -d'x' -f2)

FONT_H=22

# 顶部 cava 高度（像素）
CAVA_PIXEL_H=$((SCREEN_H * 25 / 100))

# ncmpcpp 高度
NCMPCPP_PIXEL_H=$((SCREEN_H - CAVA_PIXEL_H - BAR_H - GAP))

# 转换为字符尺寸
COLS=191
CAVA_ROWS=$((CAVA_PIXEL_H / FONT_H))
NCMPCPP_ROWS=$((NCMPCPP_PIXEL_H / FONT_H))

# 位置
CAVA_Y=$BAR_H
NCMPCPP_Y=$((BAR_H + CAVA_PIXEL_H + GAP))

# 关闭旧窗口
pkill -f music_cava
pkill -f music_ncmpcpp

mpd_pid=$(pgrep -x mpd)
if [ -z "$mpd_pid" ]; then
    nohup mpd > /dev/null 2>&1 &
fi

# 启动 cava
st -t music_cava \
   -g "${COLS}x${CAVA_ROWS}+0+${CAVA_Y}" \
   -c float \
   -e cava &

sleep 0.2

# 启动 ncmpcpp
st -t music_ncmpcpp \
   -g "${COLS}x${NCMPCPP_ROWS}+0+${NCMPCPP_Y}" \
   -c float \
   -e ncmpcpp &
