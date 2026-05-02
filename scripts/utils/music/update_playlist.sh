#!/bin/bash

cd ~/Music && rm -rf 热歌榜

# 每日热歌榜
yun 'https://music.163.com/#/playlist?id=3778678' -c 10 -p false -q 192 -s

# 民谣歌单
yun 'https://music.163.com/#/playlist?id=919939187' -c 10 -p false -q 192 -s

# 个人歌单更新
yun 'https://music.163.com/#/my/m/music/playlist?id=2656984788' -c 10 -p false -q 192 -s

mpc update
