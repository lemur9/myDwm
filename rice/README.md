# dwm 配套主题

这套配置使用一套统一的深色玻璃主题：`#11111b` 背景、`#cdd6f4` 正文、蓝色 `#89b4fa` 主强调、紫色 `#cba6f7` 次强调、粉红 `#f38ba8` 紧急状态。

## 这版 DWM 改了什么

- 悬浮胶囊 bar：bar 窗口整体内缩，tags、布局、标题、每个状态模块、托盘各自是一颗圆角半透明胶囊，胶囊之间完全透明、直接露出壁纸。
- 当前 Tag 和当前标题用内嵌高亮胶囊表示（当前 Tag 是蓝底深字），不再使用底部指示条和大面积红底。
- 已占用 Tag 使用小圆点；空 Tag 的字形压暗，层级更清楚。
- 普通、选中、悬停、隐藏、紧急窗口使用不同视觉状态：只有选中/悬停/紧急的标题才有内嵌胶囊，普通标题只显示文字。
- 内存、音乐、音量、时间、中文星期和日期显示为可点击的彩色胶囊模块。
- 胶囊圆角自己做抗锯齿（`barfeather` 层渐淡外扩）+ 1px 高光描边（`barborder`），因为 X 核心协议的弧线没有抗锯齿。
- 颜色按 alpha 预乘（`barpremult`）：ARGB 窗口内容在合成器里按预乘解释，不预乘的话低 alpha 的颜色会偏浓。
- 8px 内间距、12px 外间距、2px 边框和单窗口 smart gaps。
- `Mod+p` 改为打开 Rofi drun。
- 修复原来只为两个 Scheme 提供 alpha、另外两个 Scheme 越界读取的问题。
- 修复托盘 selection 抢占失败时 `getsystraywidth()` 解引用空指针的问题。

## 悬浮胶囊 bar 的调节点

都在 `config.h` 顶部，改完重新编译：

| 变量 | 默认 | 作用 |
|---|---|---|
| `barheight` | 30 | 胶囊本体高度 |
| `barmarginx` / `barmarginy` | 10 / 6 | 胶囊到屏幕左右、上下的距离（悬浮留白） |
| `barradius` | 15 | 分区胶囊圆角；等于 `barheight/2` 时是完整药丸 |
| `islandgap` | 6 | 分区胶囊之间的空隙 |
| `islandpad` | 8 | 胶囊内部左右留白 |
| `pillinset` / `tagradius` | 4 / 9 | 内嵌高亮胶囊的内缩量和圆角 |
| `tabgap` | 4 | 标题胶囊里各 tab 之间的间隔 |
| `statusgap` | 6 | 状态胶囊之间的空隙 |
| `barfeather` | 2 | 圆角抗锯齿层数，0 关闭；同时决定 bar 窗口比胶囊大多少 |
| `barborder` | 1 | 胶囊 1px 高光描边开关 |
| `baralpha` | 0xa0 | 胶囊底色透明度，约 63% |
| `barpremult` | 1 | 颜色按 alpha 预乘，觉得颜色不对可以改 0 对比 |

注意 `barmarginy` 要 >= `barfeather`，否则 bar 窗口会伸出布局预留的那一条。

布局预留高度 `bh = barheight + 2 * barmarginy`（默认 42px），比原来的 30px 高，
窗口区域会相应变矮一点；嫌占地方就把 `barmarginy` 调到 4。

窄屏或者状态文本过长时按顺序降级：先丢掉标题胶囊，再丢掉布局胶囊，
保证右边的时钟和日期始终完整可见。1024px 以下同时播放长曲名时，
状态胶囊会盖住 tags 胶囊的右侧，这时把 `statuspadding` 或 `islandpad` 调小即可。

## 文件

```text
rice/
├── picom/picom.conf     # 圆角、阴影、淡入淡出、dual_kawase 模糊
├── rofi/config.rasi     # 应用/运行/窗口启动器
├── dunst/dunstrc        # 通知和音量进度条
├── st/theme.h           # st 0.9.x 外观配置块
├── st/Xresources        # st xresources 补丁的可选配置
└── install.sh           # 安装前三项到 ~/.config
```

## 依赖

至少需要：

- `picom`、`rofi`、`dunst`、`libnotify`
- `xsetroot`（通常在 `xorg-xsetroot` 或 `x11-xserver-utils` 中）
- JetBrainsMono Nerd Font
- Noto Sans CJK SC
- 推荐 Papirus Dark 图标主题
- 状态栏音量需要 `pactl` 或 `wpctl`
- 音乐控制需要 `mpd`、`mpc`，播放器窗口需要 `ncmpcpp`，网络歌单同步需要 `yun`
- 日历通知需要 `cal`

Arch Linux 示例：

```sh
sudo pacman -S picom rofi dunst libnotify papirus-icon-theme \
  noto-fonts-cjk xorg-xsetroot
# Nerd Font 包名随仓库而异，常见为 ttf-jetbrains-mono-nerd
```

Debian/Ubuntu 示例：

```sh
sudo apt install picom rofi dunst libnotify-bin papirus-icon-theme \
  fonts-jetbrains-mono fonts-noto-cjk x11-xserver-utils
```

Debian 官方的 JetBrains Mono 通常不包含 Nerd Font 图标，需要额外安装 Nerd Fonts 版本，否则 Tag 和状态栏图标会显示方块。

## 安装 Picom、Rofi 和 Dunst 配置

```sh
cd /path/to/dwm
./rice/install.sh
```

安装脚本会在覆盖普通文件前生成带时间戳的备份。它不会自动修改或编译 st，也不会安装系统软件。

手动安装等价于：

```sh
mkdir -p ~/.config/{picom,rofi,dunst}
cp rice/picom/picom.conf ~/.config/picom/picom.conf
cp rice/rofi/config.rasi ~/.config/rofi/config.rasi
cp rice/dunst/dunstrc ~/.config/dunst/dunstrc
```

## st

st 的 `config.h` 是源码的一部分，不是放在 `~/.config` 的运行时配置，而且不同补丁版本的结构可能不同。因此 `st/theme.h` 有意只提供稳定的外观部分。

1. 打开你的 st 源码目录中的 `config.h`。
2. 用 `rice/st/theme.h` 中的同名声明替换字体、边距、色板和默认颜色声明。
3. 不要直接 `#include "theme.h"` 后保留原声明，否则会重定义。
4. 重新编译：

```sh
make clean
sudo make install
```

如果你的 st 有 Xresources 补丁，也可以尝试：

```sh
cat rice/st/Xresources >> ~/.Xresources
xrdb -merge ~/.Xresources
```

不同 Xresources 补丁使用的键名略有差异；如果不生效，以你的补丁文档为准。

### st 透明度

本配置默认保持终端文字清晰，没有让 Picom 把整个窗口（包括文字）一起变淡。如果你的 st 有 alpha 补丁，可以在它自己的 `config.h` 中设置：

```c
float alpha = 0.94;
```

具体变量可能叫 `alpha` 或 `alphaUnfocused`，取决于补丁版本。

## DWM 和状态栏

确认 `config.h` 中的 `workspace` 指向 DWM 配置实际所在目录。当前仓库原值是：

```c
static const char workspace[] = "/tool/dwm";
```

如果你的目录不同，请修改后重新编译。DWM 启动时会把该值导出为 `$DWM`，状态栏点击命令和 autostart 都依赖它。

不要同时运行多个根窗口状态提供者，否则它们会互相覆盖标题，表现为时间和日期来回切换。配套 autostart 默认启动 `status.sh`，并停止已经运行的 `slstatus`/`dwmblocks`；`status.sh` 本身也使用进程锁阻止重复实例。

如果你想继续使用外部状态栏而不是本主题的状态模块，请在启动 DWM 前设置：

```sh
export DWM_USE_BUNDLED_STATUS=0
```

内存和音量字段使用固定宽度，因此数值从一位变成两位或三位时不会推动相邻模块。音乐图标始终显示；只有 MPD 正在播放时才附加歌曲名，长标题会截断而不会滚动。

状态模块点击行为：

| 模块 | 左键 | 中键 | 右键 | 滚轮 |
|---|---|---|---|---|
| 内存 | 英文系统摘要 | 英文系统摘要 | 打开/关闭唯一的 btop/htop 窗口 | — |
| 音乐 | 播放/暂停 | 打开/关闭 ncmpcpp | Rofi 音乐菜单 | 上一首/下一首 |
| 音量 | 沿用原音量脚本 | 沿用原音量脚本 | 沿用原音量脚本 | 沿用原音量脚本 |
| 时间 | 番茄钟 | 健康提醒 | 取消番茄钟 | — |
| 日期 | 日历 | 任务列表 | 新建任务 | 过期/未来任务 |

音乐同步会串行报告三个歌单的结果、音乐文件总数变化、MPD 数据库状态、耗时和日志路径。只有完整成功后才进入一小时冷却；失败可以立即重试。

## 启动与重新加载

```sh
pkill picom 2>/dev/null || true
pkill dunst 2>/dev/null || true
pkill slstatus 2>/dev/null || true

picom --config ~/.config/picom/picom.conf -b
dunst -config ~/.config/dunst/dunstrc &
./scripts/statusbar/status.sh &
```

然后重新编译并重启 DWM：

```sh
make clean && make
sudo make install
# Mod+Shift+Q 后重新进入 X 会话
```

## 常见问题

### Picom 报 `dual_kawase` 不支持

你的 Picom 构建可能没有对应后端。先把 `picom.conf` 中 blur 的 method 改为：

```conf
method = "kernel";
```

或者暂时使用 `method = "none";` 验证其他配置。

### Rofi 图标为空

安装 Papirus 图标主题，或把 `config.rasi` 中的 `icon-theme` 改成系统已有主题。

### Nerd Font 图标变成方块

确认下面的命令能找到字体：

```sh
fc-match "JetBrainsMono Nerd Font Mono"
```

### 状态栏不更新

检查 `$DWM` 和 `workspace` 是否正确，再手动运行：

```sh
$DWM/scripts/statusbar/status.sh
```

### 胶囊之间出现一条模糊的矩形带

Picom 的 `blur-background` 是按窗口矩形生效的，不看像素 alpha，所以整条 bar 窗口
（包括胶囊之间的空隙和四周的抗锯齿留白）都会被模糊。深色平滑壁纸下看不出来，
胶囊还能拿到真正的磨砂玻璃质感，因此默认保留模糊。壁纸细节多、看着碍眼的话，
在 `picom.conf` 的 `blur-background-exclude` 里加上：

```conf
  "class_g = 'dwm'",
```

代价是胶囊只剩半透明、没有磨砂感。

### 胶囊颜色比 config.h 里的色值浅 / 深

`barpremult` 默认打开：颜色会先按 alpha 预乘再写进 ARGB 窗口，这是合成器期望的格式，
`baralpha = 0xa0` 就是真正的 63% 混合。如果你的合成器/驱动把窗口内容当成非预乘处理，
胶囊会显得偏浓，这时把 `barpremult` 改成 0 再编译对比一下。

### 托盘图标跑到胶囊外面 / 变成方块

托盘现在是盖在托盘胶囊正上方的一颗独立透明窗口，胶囊底色由 bar 窗口垫。
图标高度按 `barheight - 2 * traypad` 缩放并在胶囊里垂直居中，改 `barheight` 后需要
重启托盘应用（图标尺寸是嵌入时协商的，只重载 Picom 或重编译 DWM 不够）。
24-bit 图标的透明区会填 `col_surface`，32-bit 图标走自己的 alpha 通道。
