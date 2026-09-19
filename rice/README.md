# myDwm 配套主题

这套配置使用一套统一的深色玻璃主题：`#11111b` 背景、`#cdd6f4` 正文、蓝色 `#89b4fa` 主强调、紫色 `#cba6f7` 次强调、粉红 `#f38ba8` 紧急状态。

## 这版 DWM 改了什么

- 30px 状态栏、固定宽度且居中的 Tag。
- 当前 Tag 与当前标题使用 3px 底部指示条，不再使用大面积红底。
- 已占用 Tag 使用小圆点。
- 普通、选中、悬停、隐藏、紧急窗口使用不同视觉状态。
- CPU/内存、音乐、音量、时间、日期显示为可点击的彩色状态模块。
- 8px 内间距、12px 外间距、2px 边框和单窗口 smart gaps。
- `Mod+p` 改为打开 Rofi drun。
- 修复原来只为两个 Scheme 提供 alpha、另外两个 Scheme 越界读取的问题。

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
- 状态栏音量需要 `pactl` 或 `wpctl`；音乐模块可选 `mpc`

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
cd /path/to/myDwm
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
export MYDWM_USE_BUNDLED_STATUS=0
```

CPU、内存和音量字段已使用固定宽度，因此数值从一位变成两位或三位时不会推动相邻模块。

状态模块点击行为：

| 模块 | 左键 | 其他 |
|---|---|---|
| CPU / 内存 | 系统摘要 | 右键打开 btop/htop（若存在） |
| 音乐标题 | 启停 MPD | 中键同步、右键歌词（沿用原脚本） |
| 播放图标 | — | 中键播放/暂停、滚轮切歌 |
| 音量 | 沿用原音量脚本 | 支持原脚本的其他按键 |
| 时间 | 番茄钟 | 沿用原脚本 |
| 日期 | 日历 | 沿用原任务脚本 |

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

### 托盘最左侧图标偶尔变成纯黑

这版托盘会使用与状态栏一致的 ARGB Visual，并发布 `_NET_SYSTEM_TRAY_VISUAL`；24-bit 和 32-bit 图标使用不同的清屏背景，首次嵌入后还会主动请求一次重绘。Picom 也不会再分别裁切 DWM bar 与 systray 的交界处。更新后需要重新编译 DWM，并完整重启托盘应用；只重载 Picom 不够。
