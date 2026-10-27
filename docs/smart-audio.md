# dwm 智能声音控制

本实现保留 **PulseAudio / libpulse** 后端，不迁移 PipeWire、不修改音频服务配置。快捷键、状态栏、GTK3 面板和通知共用一个会话服务的真实状态。

> 仓库代码已实现，**不代表已经在你的桌面安装、启动或通过实机验收**。本次开发没有部署配置，也没有重启 dwm、Dunst、PulseAudio 或其他桌面组件。测试范围见文末。

## 1. 安装依赖

需要 Linux `/proc`、X11、Python **3.10+**、libpulse、`pulsectl`、`python-xlib`、GTK **3** 和 PyGObject。`notify-send` 为可选通知依赖（推荐配合 Dunst）。不需要 `pamixer`、Rofi 音频菜单或终端混音器。

Arch Linux：

```sh
sudo pacman -S python python-pulsectl python-xlib python-gobject gtk3 libpulse libnotify
```

Debian / Ubuntu（发行版 Python 需为 3.10 或以上）：

```sh
sudo apt install python3 python3-pulsectl python3-xlib python3-gi \
  gir1.2-gtk-3.0 libpulse0 libnotify-bin
```

如果发行版提供的 `pulsectl` 过旧，可以只在虚拟环境中安装 Python 音频依赖。GTK / PyGObject 仍使用系统包，**不要用 pip 安装 GTK**：

```sh
python3 -m venv --system-site-packages ~/.local/share/dwm-audio-venv
~/.local/share/dwm-audio-venv/bin/pip install -r scripts/utils/sound/requirements.txt
# 在启动 dwm 前设置，让服务、面板、状态栏使用同一个 Python：
export DWM_AUDIO_PYTHON="$HOME/.local/share/dwm-audio-venv/bin/python"
```

`requirements.txt` 的受支持范围是 `pulsectl>=24.4,<25`、`python-xlib>=0.33,<1`。服务与面板按需加载原生依赖，状态栏客户端本身不加载 GTK / libpulse。

## 2. 在真实桌面启用

先在 **dwm 的 X11 会话内**进行只读检查：

```sh
./scripts/utils/sound/audioctl doctor
```

确认 `config.h` 的 `workspace` 是你本机仓库的绝对路径（仓库原值 `/tool/dwm`），而不是沙箱路径。dwm 导出的 `$DWM` 被快捷键和 autostart 使用。

准备好启用时，再自行编译并安装 dwm：

```sh
make
sudo make install
# 自行重新进入桌面会话，使新增的 Win+Shift+F1/F2/F3 和浮动规则生效。
```

新的 `autostart.sh` 会启动唯一的会话声音服务。重复运行 `audioctl start` 或重新启动 dwm **不会重建已有服务、丢失应用锁定**。声音服务只连接已有音频服务器，连接失败时等待恢复，绝不自动启动/替换 PulseAudio。

不想自动启动声音服务时，在进入 dwm 前设置 `DWM_USE_SMART_AUDIO=0`。这只关闭 autostart；显式音量操作仍可按需启动服务。状态栏读取不会自动启动服务，也不会偷偷回退为显示另一个后端/目标的音量。

手动启动和检查：

```sh
./scripts/utils/sound/audioctl start
./scripts/utils/sound/audioctl panel apps
./scripts/utils/sound/audioctl panel devices
./scripts/utils/sound/audioctl state   # 当前共享 JSON，不自动启动
./scripts/utils/sound/audioctl watch   # 订阅后续状态变化
./scripts/utils/sound/audioctl status  # 状态栏短文本，不自动启动
```

Dunst 的配套规则现按 `appname = "dwm-audio"` 匹配。若要使用新通知样式，可自行用 `rice/install.sh` 安装配置，并在合适时重新加载 Dunst；音频功能不依赖安装主题文件。

## 3. 控制规则与入口

智能操作的顺序：

1. 已锁定的稳定应用身份；
2. 原前台窗口关联且有可控制流的应用（即使流暂停）；
3. 唯一正在播放的后台应用；
4. 多个后台播放应用：打开选择面板，**本次音量/静音不执行，也不在选择后补执行**；
5. 没有可控制的应用流：默认输出设备。

后台“播放中”以 PulseAudio 的非 corked 流为依据，不通过媒体标题猜测，也不检测声波是否正好为零。只读/无音量能力的流不会成为智能音量目标。

| 操作 | 行为 |
| --- | --- |
| 状态栏音量区左键 | 智能静音切换 |
| 中键 / 右键 | 应用页 / 设备页 |
| 滚轮上 / 下 | 智能音量 +1 / −1 个百分点 |
| Win+F1 / F2 / F3 | 智能静音 / −5 / +5 |
| Win+Shift+F1 / F2 / F3 | **仅默认输出设备**静音 / −5 / +5，不受应用锁定影响 |

音量写入限制在 0–100%。音量为 0 不等于静音；滑块和增减音量不会取消静音。外部软件设置超过 100% 时读取仍显示实际数值，下一次本系统写入会限制到范围内。

锁定只在服务内存中保存，不绑定 PID 或流编号，不跨桌面会话/声音服务重启。音频服务器重启不重启声音服务，因此锁定保留。锁定应用退出或暂时没有可控制流时，提示不可用，**绝不调节其他应用或系统声音**。可在面板顶部或 CLI 解锁。

设备总音量快捷键执行后，状态栏仍表示智能目标；通知明确说明这次实际操作的默认设备。

## 4. 原生浮窗

- `DwmAudio` 类、深色/蓝色主题、两个页面、当前目标/输出/锁定状态。
- 鼠标入口按鼠标所在显示器定位；快捷键产生的歧义按原窗口所在显示器定位，使用工作区边界限制尺寸与位置。
- 同一入口再次触发关闭，另一入口在同一进程切页。Esc、失去焦点或外部点击关闭。XInput2 的外部点击监听是被动的，不抢占鼠标或键盘；没有 XInput2 时退化为失焦关闭。
- Tab / Shift+Tab 在控件间导航；方向键操作列表、下拉框和滑块；Ctrl+1 / Ctrl+2 切页；Ctrl+U 解锁。歧义模式的应用行可以 Enter 选择并锁定。
- 普通浏览、选择行、展开流不隐式锁定，只有明确的“锁定”按钮改变锁定。歧义模式选择后锁定并关闭面板。
- 打开面板前冻结原窗口上下文；面板自身获得焦点不会成为新的控制对象。
- 音频事件实时更新，应用/设备行按稳定 key 复用，排序不重建整页。刷新不取消展开、不重置选择、不抢走正在拖动或等待写入确认的滑块。
- 同一应用的多流默认分组；相对增减分别作用于各条可写流；应用滑块绝对值统一设置可写流。多流音量显示范围（例如 `25–75%`）、部分静音单独标记。展开后可分别控制**音量与静音**，并查看媒体、流编号、采样率、声道及实际输出。输出路由和持久化偏好是**应用级**，不为临时流编号保存偏好。
- 浏览器多进程关联使用窗口 PID、进程祖先和精确 WM_CLASS/音频属性别名。共享 shell/systemd 祖先本身不构成匹配。Chrome 与 Chromium 不混为一组，不猜测标签页对应关系。

稳定身份优先使用音频属性 `application.id`（已知桌面 ID 与二进制别名统一），其次 `application.process.binary` / `/proc`，最后应用名称。设备使用 PulseAudio 的稳定 sink name，绝不保存临时 sink index。应用完全不提供身份属性或改变自身 ID、设备更换声卡配置导致 sink name 改变时，可能需要重新选择；不能凭不可靠信息假装识别成功。

## 5. 应用路由与输出记忆

应用页选择输出后，迁移该应用全部流并长期记住设备；“记住当前”也能记住已经在使用的输出。

偏好设备不可用时：

1. 优先使用可用默认输出，否则按稳定 sink name 排序选择可用备用设备；
2. 迁移受影响流，通知应用、原设备和实际备用设备；
3. 保留原偏好，不把回退写成新偏好；
4. 原设备可用时自动切回，之后新建流也使用该偏好；
5. 中途手动选择新设备，以最新选择为准。

端口明确报告 `no` 或 sink 消失才视为不可用；`SUSPENDED`、`IDLE` 和端口 `unknown` 不表示断开。所有设备都不可用时保留偏好与锁定，显示等待输出。音频服务恢复连接后重新读取实际状态并应用路由。

设置默认输出只改变默认设备，**不会批量迁移已有应用**，有专属偏好的应用继续使用自身路由。没有偏好的应用也只在原输出不可用时回退，不因默认设备改变而被本服务强行搬动。

“取消记忆”移除本服务偏好，将该应用当前流送回可用默认输出，并仅清除这些流对应的 PulseAudio `module-stream-restore` 设备记录，保留该模块的音量、静音及其他应用记录。后续交还系统路由策略。若音频服务器没有该扩展或当前迁移失败，会明确反馈部分完成，不假报成功。

手动路由先持久化**最新意图**，再迁移并回读结果。遇到部分失败时，面板仍显示实际输出和错误，新意图保留以便重试；通知不会把意图当作已经完成的迁移。损坏或未知版本的偏好文件不会被静默覆盖，需备份修复后重启声音服务。

## 6. 文件、进程与调试

```text
scripts/utils/sound/
├── audioctl                 # 可执行入口；支持 DWM_AUDIO_PYTHON
├── audio.py                 # Python 入口
├── requirements.txt         # 纯 Python 依赖
├── mixer.sh                 # 兼容旧入口 → 原生设备页
└── smart_audio/
    ├── model.py, identity.py # 快照、分组、窗口关联与目标选择
    ├── backend.py           # 结构化 libpulse + 独立事件连接
    ├── engine.py            # 串行操作、回读确认、路由策略
    ├── storage.py           # 版本化偏好、私有会话路径
    ├── ipc.py, service.py    # Unix socket、单实例、X11/音频事件
    ├── x11.py, notify.py     # 焦点上下文、合并通知
    └── panel.py, panel.css   # 按需启动的 GTK3 UI
```

- 偏好：`${XDG_STATE_HOME:-~/.local/state}/dwm-audio/preferences.json`，文件 0600、目录 0700，临时文件 + `fsync` + 原子替换。
- IPC / 锁 / 日志：`${XDG_RUNTIME_DIR:-/tmp}/dwm-audio-UID-SESSIONHASH/`。哈希包含 X display 与 `XDG_SESSION_ID`。运行目录 0700、socket 0600，客户端和服务端双向校验 Linux peer UID。`doctor` 输出实际路径。
- 调试可设置 `DWM_AUDIO_RUNTIME_DIR`、`DWM_AUDIO_STATE_DIR`，客户端和服务必须使用相同环境。运行目录必须属于本人且为 0700，不允许符号链接。
- 全部音频读写在服务线程串行执行。事件订阅使用另一个 libpulse 连接，仅唤醒服务，不在 libpulse 回调内嵌套音频操作。
- 音频变化事件合并约 40ms；断线及路由失败约每 2 秒重试，事件连接指数退避重连；状态栏沿用 `STATUS_INTERVAL`（默认 1 秒）。IPC 是有界非阻塞连接，慢客户端不会持有服务线程。
- 通知在独立线程合并发送，连续音量调整复用 ID/stack tag。缺少通知程序不影响音频操作。
- X11 连接关闭会退出服务，丢弃会话锁定。主动 `audioctl stop` 也会关闭面板、移除 socket，但保留路由偏好。PID 锁文件保留 inode，防止删除锁后双实例竞态。

常用 CLI：

```sh
./scripts/utils/sound/audioctl adjust -5
./scripts/utils/sound/audioctl mute
./scripts/utils/sound/audioctl adjust +5 --device
./scripts/utils/sound/audioctl mute --device
./scripts/utils/sound/audioctl lock 'app:mpv'  # key 必须以 state 中实际值为准
./scripts/utils/sound/audioctl unlock
./scripts/utils/sound/audioctl route 'app:mpv' '实际的 sink name'
./scripts/utils/sound/audioctl forget 'app:mpv'
./scripts/utils/sound/audioctl set-volume 64 --app 'app:mpv'
./scripts/utils/sound/audioctl set-mute on --stream 10 --owner 'app:mpv'
./scripts/utils/sound/audioctl stop
```

旧 `vol.sh change 0` 现在正确地执行**智能静音**；`vol.sh device 0` 执行默认设备静音。`mixer.sh` 保留为设备页兼容入口。音量脚本中已移除 `pkill -USR1 slstatus`、本地化 `pactl` 文本解析、双层 Rofi 菜单和异步“乐观成功”终端面板。

## 7. 验证范围与实机验收

无需音频、GTK 或 X11 即可运行自动化测试（只需 Python 标准库）：

```sh
make test
# 等价：python3 -m unittest discover -s tests -v
```

2026-10-27 沙箱验证：**93 项自动化测试通过**；Python 编译、Shell 语法、Ruff E9/F 和 `git diff --check` 通过。`make` 因缺少 `X11/Xlib.h` 未能完成，`doctor` 如实报告原生依赖与桌面环境缺失。

测试包括真实 Unix socket 往返、状态订阅、慢/坏客户端、CLI/旧脚本映射、工作区边界计算，以及可注入音频快照和进程树的策略测试。**模拟后端通过不等于真实设备已经验收。**

| 场景 | 自动化覆盖 | 仍需真实桌面验证 |
| --- | --- | --- |
| 前台视频 + 后台音乐，音量/静音同目标 | 目标优先级与操作范围 | 真实应用属性、焦点及通知 |
| 前台无音频，后台一个/多个应用 | 唯一目标 / 歧义不执行 / 选择后不补执行 | GTK 选择体验 |
| 无音频流、默认设备独立快捷键 | 默认设备范围，不受锁定影响 | dwm 按键、bar 点击 |
| 锁定后切窗、退出、重建音频流、解锁 | 锁定身份与不可用保护 | 实际应用生命周期 |
| 打开面板保留上下文 | 服务上下文冻结、面板过滤、单实例切页 | 多显示器、边界、外部点击、Esc、键盘导航 |
| 多条流、混合音量/静音、单流控制 | 分组、范围/部分静音、单流 owner 校验 | Chrome 音频子进程元数据、滑块交互 |
| 状态栏/面板共享实时状态 | socket 订阅、revision、CLI 状态栏一致 | GTK 刷新不打断拖动、Dunst 样式 |
| 偏好重启持久、断开/重连/最新选择 | 稳定身份/设备名、回退/恢复、原偏好保留 | USB / 蓝牙实际热插拔与 PulseAudio 事件 |
| 全部输出消失再恢复、空闲挂起 | 等待状态、恢复判断、不误判 SUSPENDED | 驱动端口可用性报告 |
| 0–100%、0 与静音分离、失败不报成功 | 边界、回读、部分失败 | 实际服务器行为 |
| 音频服务重启 | 模拟断线恢复并保留锁定 | 真实 libpulse 事件连接重连 |

本次沙箱缺少 X11 / GTK3 / libpulse 原生库与真实音频设备，也缺少 dwm 的 X11 开发头文件。因此不能在这里声称 GTK 运行、桌面完整编译、硬件热插拔或听音验收已经通过。请在本机依次验证上表右栏，尤其是蓝牙连接切换、多屏边缘和 Chrome 多流。
