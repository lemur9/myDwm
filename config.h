/* See LICENSE file for copyright and license details. */

/* 外观：Catppuccin Mocha inspired glass theme */
static const int newclientathead    = 0;        /* 定义新窗口在栈顶还是栈底 */
static const unsigned int borderpx  = 2;        /* 细强调色边框，Picom 再负责阴影与圆角 */
static const unsigned int snap      = 32;       /* 窗口边框捕捉范围 */
static const unsigned int gappih    = 8;        /* 窗口之间的水平间距 */
static const unsigned int gappiv    = 8;        /* 窗口之间的垂直间距 */
static const unsigned int gappoh    = 12;       /* 窗口与屏幕边缘的水平间距 */
static const unsigned int gappov    = 12;       /* 窗口与屏幕边缘的垂直间距 */
static const int smartgaps          = 1;        /* 单窗口时去掉外部间距 */
static const unsigned int systraypinning = 0;   /* 0: systray 跟随当前显示器 */
static const unsigned int systrayspacing = 6;   /* 托盘图标留白 */
static const int systraypinningfailfirst = 1;
static const int showsystray        = 1;
static const int showbar            = 1;
static const int topbar             = 1;

/* 栏高和留白单独配置，避免字号变化破坏布局。 */
static const unsigned int barheight       = 30;
static const unsigned int barlrpad        = 16; /* 文本左右留白总和 */
static const unsigned int tagwidthpx      = 36; /* Tag 固定点击宽度 */
static const unsigned int tagindicator    = 3;  /* 当前 Tag / tab 底部指示条 */
static const unsigned int statuspadding   = 9;  /* 状态模块单侧内边距 */
static const unsigned int statusgap       = 3;  /* 状态模块之间的间隔 */

static const char *fonts[] = {
  "JetBrainsMono Nerd Font Mono:style=SemiBold:size=12",
  "Noto Sans CJK SC:size=11",
  "monospace:size=12"
};

/* 调色板 */
static const char col_bg[]          = "#11111b";
static const char col_surface[]     = "#1e1e2e";
static const char col_hover[]       = "#313244";
static const char col_border[]      = "#45475a";
static const char col_text[]        = "#cdd6f4";
static const char col_muted[]       = "#6c7086";
static const char col_blue[]        = "#89b4fa";
static const char col_lavender[]    = "#b4befe";
static const char col_mauve[]       = "#cba6f7";
static const char col_red[]         = "#f38ba8";
static const char col_teal[]        = "#94e2d5";
static const char col_sky[]         = "#89dceb";
static const char col_yellow[]      = "#f9e2af";
static const char col_stat_system[] = "#192a3a";
static const char col_stat_music[]  = "#2a243b";
static const char col_stat_volume[] = "#1d2b42";
static const char col_stat_clock[]  = "#1b3132";
static const char col_stat_date[]   = "#322e25";

static const unsigned int baralpha    = 0xea; /* 约 92%，配合 Picom 模糊 */
static const unsigned int borderalpha = OPAQUE;
static const char *colors[][3] = {
  /*                      fg            bg                border */
  [SchemeNorm]       = { col_text,      col_bg,            col_border   },
  [SchemeSel]        = { col_blue,      col_surface,       col_blue     },
  [SchemeHov]        = { col_lavender,  col_hover,         col_lavender },
  [SchemeHid]        = { col_muted,     col_bg,            col_border   },
  [SchemeUrg]        = { col_red,       col_surface,       col_red      },
  [SchemeStatSystem] = { col_sky,       col_stat_system,   col_sky      },
  [SchemeStatMusic]  = { col_mauve,     col_stat_music,    col_mauve    },
  [SchemeStatVolume] = { col_blue,      col_stat_volume,   col_blue     },
  [SchemeStatClock]  = { col_teal,      col_stat_clock,    col_teal     },
  [SchemeStatDate]   = { col_yellow,    col_stat_date,     col_yellow   },
};
static const unsigned int alphas[][3] = {
  /*                      fg      bg        border */
  [SchemeNorm]       = { OPAQUE, baralpha, borderalpha },
  [SchemeSel]        = { OPAQUE, baralpha, borderalpha },
  [SchemeHov]        = { OPAQUE, baralpha, borderalpha },
  [SchemeHid]        = { OPAQUE, baralpha, borderalpha },
  [SchemeUrg]        = { OPAQUE, baralpha, borderalpha },
  [SchemeStatSystem] = { OPAQUE, baralpha, borderalpha },
  [SchemeStatMusic]  = { OPAQUE, baralpha, borderalpha },
  [SchemeStatVolume] = { OPAQUE, baralpha, borderalpha },
  [SchemeStatClock]  = { OPAQUE, baralpha, borderalpha },
  [SchemeStatDate]   = { OPAQUE, baralpha, borderalpha },
};

/* tagging */
static const char *tags[] = { "󰣇", "", "", "󰘅", "", "", "", "", "" };

static const Rule rules[] = {
  /* xprop(1):
     *	WM_CLASS(STRING) = instance, class
     *	WM_NAME(STRING) = title
     */
  /* class      instance    title       tags mask     isfloating   monitor */
  { "Gimp",     NULL,       NULL,       0,            1,           -1 },
  { "chrome",   NULL,       NULL,       1 << 2,       0,           -1 },
  { "QQ",       NULL,       NULL,       1 << 3,       0,            0 },
  { "wechat",   NULL,       NULL,       1 << 4,       0,            0 },
  { "steam",    NULL,       NULL,       1 << 6,       0,           -1 },
  { "obs",      NULL,       NULL,       1 << 7,       0,           -1 },
  { NULL,       NULL,       "画中画",    0,            1,            1 },
  { NULL,       NULL,       "预览",      0,            1,           -1 },

  /** 部分特殊class的规则 */
  {"float",     NULL,       NULL,       0,            1,           -1 }, // class = float       浮动
};

/* layout(s) */
static const float mfact     = 0.55; /* 主区域与堆栈之间的面积配比 */
static const int nmaster     = 1;    /* number of clients in master area */
static const int resizehints = 0;    /* 平铺时忽略应用尺寸增量，避免产生难看的空洞 */
static const int lockfullscreen = 1; /* 1 will force focus on the fullscreen window */

static const Layout layouts[] = {
  /* symbol     arrange function */
  { "[]=",      tile },    /* first entry is default */
  { "[M]",      monocle },
  { "[G]",      magicgrid },    /* 网格 */
};

/* key definitions */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
  { MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
  { MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
  { MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static char dmenumon[2] = "0"; /* spawn() 仍会更新该值；Rofi 自行跟随当前显示器 */
static const char *dmenucmd[] = { "rofi", "-show", "drun", NULL };
static const char *termcmd[]  = { "st", NULL };

static const char workspace[] = "/tool/dwm";
static const char *autostartscript = "$DWM/scripts/utils/autostart.sh";

/* commands spawned when clicking statusbar, the mouse button pressed is exported as BUTTON */
static const StatusCmd statuscmds[] = {
  { "notify-send Mouse$BUTTON",                 0 },
  { "$DWM/scripts/statusbar/system.sh",         1 },
  { "$DWM/scripts/statusbar/music.sh",          4 },
  { "$DWM/scripts/statusbar/playlist.sh",       5 },
  { "$DWM/scripts/statusbar/vol.sh",            6 },
  { "$DWM/scripts/statusbar/clock.sh",          7 },
  { "$DWM/scripts/statusbar/date.sh",           8 },
};

static const char *statuscmd[] = { "/bin/sh", "-c", NULL, NULL };

static const Key keys[] = {
  /* modifier                     key        function        argument */
  // 菜单栏
  { MODKEY,                       XK_p,      spawn,          {.v = dmenucmd } },
  // 终端
  { MODKEY|ShiftMask,             XK_Return, spawn,          {.v = termcmd } },
  // 状态栏展示
  { MODKEY,                       XK_b,      togglebar,      {0} },
  // 窗口切换
  { MODKEY,                       XK_j,      focusstackvis,  {.i = +1 } },
  { MODKEY,                       XK_k,      focusstackvis,  {.i = -1 } },
  // 窗口切换带隐藏窗口
  { MODKEY|ShiftMask,             XK_j,      focusstackhid,  {.i = +1 } },
  { MODKEY|ShiftMask,             XK_k,      focusstackhid,  {.i = -1 } },
  // 调整主区域的窗口数量
  { MODKEY,                       XK_i,      incnmaster,     {.i = +1 } },
  { MODKEY,                       XK_d,      incnmaster,     {.i = -1 } },
  // 调整主区域的窗口宽度
  { MODKEY,                       XK_h,      setmfact,       {.f = -0.05} },
  { MODKEY,                       XK_l,      setmfact,       {.f = +0.05} },

  // 设为主窗口
  { MODKEY,                       XK_Return, zoom,           {0} },
  // 切换tag
  { MODKEY,                       XK_Tab,    view,           {0} },
  // 关闭窗口
  { MODKEY|ShiftMask,             XK_c,      killclient,     {0} },
  // 布局
  { MODKEY,                       XK_t,      setlayout,      {.v = &layouts[0]} },
  { MODKEY,                       XK_m,      setlayout,      {.v = &layouts[1]} },
  { MODKEY,                       XK_g,      setlayout,      {.v = &layouts[2]} },
  // 切换布局
  { MODKEY,                       XK_space,  setlayout,      {0} },
  // 切换浮动
  { MODKEY|ShiftMask,             XK_space,  togglefloating, {0} },

  // 间隙模式切换
  { MODKEY|Mod1Mask,              XK_0,      togglegaps,     {0} },
  // 恢复默认窗口间隙
  { MODKEY|Mod1Mask|ShiftMask,    XK_0,      defaultgaps,    {0} },
  // 显示所有tag上的所有窗口
  { MODKEY,                       XK_0,      view,           {.ui = ~0 } },
  // 所有tag显示此tag的活跃窗口
  { MODKEY|ShiftMask,             XK_0,      tag,            {.ui = ~0 } },

  // 多显示器切换
  { MODKEY,                       XK_comma,  focusmon,       {.i = -1 } },
  { MODKEY,                       XK_period, focusmon,       {.i = +1 } },
  // 多显示器窗口移动
  { MODKEY|ShiftMask,             XK_comma,  tagmon,         {.i = -1 } },
  { MODKEY|ShiftMask,             XK_period, tagmon,         {.i = +1 } },

  // 窗口隐藏和显示
  { MODKEY,                       XK_s,      show,           {0} },
  { MODKEY|ShiftMask,             XK_s,      showall,        {0} },
  { MODKEY|ShiftMask,             XK_h,      hide,           {0} },

  // 打开一个浮动窗口
  { MODKEY,                       XK_f,      spawn,          SHCMD("st -c float") },

  // 调整窗口
  { MODKEY|ControlMask,  XK_Up,           movewin,          {.ui = UP} },
  { MODKEY|ControlMask,  XK_Down,         movewin,          {.ui = DOWN} },
  { MODKEY|ControlMask,  XK_Left,         movewin,          {.ui = LEFT} },
  { MODKEY|ControlMask,  XK_Right,        movewin,          {.ui = RIGHT} },

  { MODKEY|Mod1Mask,     XK_Up,           resizewin,        {.ui = V_REDUCE} },        /* super alt up       |  调整窗口 */
  { MODKEY|Mod1Mask,     XK_Down,         resizewin,        {.ui = V_EXPAND} },        /* super alt down     |  调整窗口 */
  { MODKEY|Mod1Mask,     XK_Left,         resizewin,        {.ui = H_REDUCE} },        /* super alt left     |  调整窗口 */
  { MODKEY|Mod1Mask,     XK_Right,        resizewin,        {.ui = H_EXPAND} },

  // dwm退出
  { MODKEY|ShiftMask,             XK_q,      quit,           {0} },

  // -------- 需重写
  // 调整窗口大小，不保留窗口间隙
  { MODKEY|Mod1Mask,              XK_h,      incrgaps,       {.i = +1 } },
  { MODKEY|Mod1Mask,              XK_l,      incrgaps,       {.i = -1 } },
  // 调整窗口大小，保留窗口间隙
  { MODKEY|Mod1Mask|ShiftMask,    XK_h,      incrogaps,      {.i = +1 } },
  { MODKEY|Mod1Mask|ShiftMask,    XK_l,      incrogaps,      {.i = -1 } },
  { MODKEY|Mod1Mask|ControlMask,  XK_h,      incrigaps,      {.i = +1 } },
  { MODKEY|Mod1Mask|ControlMask,  XK_l,      incrigaps,      {.i = -1 } },
  { MODKEY,                       XK_y,      incrihgaps,     {.i = +1 } },
  { MODKEY,                       XK_o,      incrihgaps,     {.i = -1 } },
  { MODKEY|ControlMask,           XK_y,      incrivgaps,     {.i = +1 } },
  { MODKEY|ControlMask,           XK_o,      incrivgaps,     {.i = -1 } },
  { MODKEY|Mod1Mask,              XK_y,      incrohgaps,     {.i = +1 } },
  { MODKEY|Mod1Mask,              XK_o,      incrohgaps,     {.i = -1 } },
  { MODKEY|ShiftMask,             XK_y,      incrovgaps,     {.i = +1 } },
  { MODKEY|ShiftMask,             XK_o,      incrovgaps,     {.i = -1 } },
  // ---------------


  /* 绑定功能键 */
  // Win+F1 静音
  { MODKEY,                       XK_F1,     spawn,          SHCMD("$DWM/scripts/statusbar/vol.sh change 0") },
  // Win+F2 音量减小5%
  { MODKEY,                       XK_F2,     spawn,          SHCMD("$DWM/scripts/statusbar/vol.sh change -5") },
  // Win+F3 音量增大5%
  { MODKEY,                       XK_F3,     spawn,          SHCMD("$DWM/scripts/statusbar/vol.sh change +5") },
  // Win+F12 截图
  { MODKEY,                       XK_F12,    spawn,          SHCMD("flameshot gui") },

  TAGKEYS(                        XK_1,                      0)
  TAGKEYS(                        XK_2,                      1)
  TAGKEYS(                        XK_3,                      2)
  TAGKEYS(                        XK_4,                      3)
  TAGKEYS(                        XK_5,                      4)
  TAGKEYS(                        XK_6,                      5)
  TAGKEYS(                        XK_7,                      6)
  TAGKEYS(                        XK_8,                      7)
  TAGKEYS(                        XK_9,                      8)
};

/* tag auto-launch: spawn cmd when clicking a tag with no clients on it */
static const TagCmd tagcmds[] = {
  /* tags mask       cmd                                               process (pgrep -x) */
  { 1 << 1,   "st",                                                   NULL },
  { 1 << 2,   "google-chrome-stable",                                 "chrome" },
  { 1 << 3,   "linuxqq",                                              "linuxqq" },
  { 1 << 4,   "wechat",                                               "wechat" },
  { 1 << 5,   "$DWM/scripts/utils/music/open_music_panel.sh",         NULL },
  { 1 << 6,   "steam",                                                "steam" },
  { 1 << 7,   "obs",                                                  "obs" },
};

/*
 * button definitions
 * click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin
 *
 * ClkTagBar	    标签栏（tag bar）	                用于切换/操作 tag（桌面/工作区）
 * ClkLtSymbol	    布局符号（layout symbol）	        切换窗口布局（如平铺/浮动/单窗口）
 * ClkStatusText	状态栏（status bar）	            通常显示系统状态，支持点击命令
 * ClkWinTitle	    窗口标题栏（window title bar）	    切换或聚焦窗口
 * ClkClientWin	    客户端窗口（窗口本体）	            移动/缩放/浮动窗口
 * ClkRootWin	    根窗口（桌面背景）	                很少用
 *
 * Button1          左键
 * Button2          中键
 * Button3          右键
 * Button4          上滚轮
 * Button5          下滚轮
 */
static const Button buttons[] = {
  /* click                    event mask              button                  function            argument */

  // 标签栏
  { ClkTagBar,            0,              Button1,        view,           {0} },
  { ClkTagBar,            0,              Button3,        toggleview,     {0} },
  { ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
  { ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },

  // 布局符号
  { ClkLtSymbol,          0,              Button1,        setlayout,      {0} },
  { ClkLtSymbol,          0,              Button3,        setlayout,      {.v = &layouts[2]} },

  // 窗口标题栏
  { ClkWinTitle,          0,              Button1,        togglewin,      {0} },
  { ClkWinTitle,          0,              Button2,        zoom,           {0} },

  // 状态栏
  { ClkStatusText,        0,              Button1,        spawn,          {.v = statuscmd } },
  { ClkStatusText,        0,              Button2,        spawn,          {.v = statuscmd } },
  { ClkStatusText,        0,              Button3,        spawn,          {.v = statuscmd } },
  { ClkStatusText,        0,              Button4,        spawn,          {.v = statuscmd } },
  { ClkStatusText,        0,              Button5,        spawn,          {.v = statuscmd } },

  // 客户端窗口
  { ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },
  { ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
  { ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
};

