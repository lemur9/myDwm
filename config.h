/* See LICENSE file for copyright and license details. */

/* 外观 */
static const int newclientathead    = 0;        /* 定义新窗口在栈顶还是栈底 */
static const unsigned int borderpx  = 1;        /* 窗口边界像素 */
static const unsigned int snap      = 32;       /* 窗口边框捕捉大小,吸附效果范围 */
static const unsigned int gappih    = 10;       /* 窗口之间的水平间距 */
static const unsigned int gappiv    = 10;       /* 窗口之间的垂直间距*/
static const unsigned int gappoh    = 10;       /* 窗口和屏幕边缘之间的水平间距 */
static const unsigned int gappov    = 10;       /* 窗口和屏幕边缘之间的垂直间距 */
static const int smartgaps          = 0;        /* 1 表示只有一个窗口时没有外部间距 */
static const unsigned int systraypinning = 0;   /* 0: sloppy systray follows selected monitor, >0: pin systray to monitor X */
static const unsigned int systrayspacing = 2;   /* systray spacing */
static const int systraypinningfailfirst = 1;   /* 1: if pinning fails, display systray on the first monitor, False: display systray on the last monitor*/
static const int showsystray        = 1;     /* 0 means no systray */
static const int showbar            = 1;        /* 0 表示不显示状态栏 */
static const int topbar             = 1;        /* 0 表示底部显示状态栏 */
static const char *fonts[]          = { "JetBrainsMono Nerd Font Mono:style=medium:size=13", "monospace:size=13" };
static const char dmenufont[]       = "monospace:size=13";
static const char col_gray1[]       = "#222222";    // 状态条底色
static const char col_gray2[]       = "#444444";    // 当static const unsigned int borderpx不为0时，非活动窗口外边框颜色
static const char col_gray3[]       = "#bbbbbb";    // 当前非活动的title字体颜色
static const char col_gray4[]       = "#eeeeee";    // 当前活动的title字体颜色
static const char col_cyan[]        = "#e55555";    // title底色
static const unsigned int baralpha = 0xd0;
static const unsigned int borderalpha = OPAQUE;
static const char *colors[][3]      = {
  /*               fg         bg         border   */
  [SchemeNorm] = { col_gray3, col_gray1, col_gray2 },
  [SchemeSel]  = { col_gray4, col_cyan,  col_cyan  },
  [SchemeHov]  = { col_gray4, col_cyan,  col_cyan  },
  [SchemeHid]  = { col_cyan,  col_gray1, col_cyan  },
};
static const unsigned int alphas[][3]      = {
  /*               fg      bg        border*/
  [SchemeNorm] = { OPAQUE, baralpha, borderalpha },
  [SchemeSel]  = { OPAQUE, baralpha, borderalpha },
};

/* tagging */
static const char *tags[] = { "󰣇", "", "", "󰘅", "", "", "", "", "" };

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
static const int resizehints = 1;    /* 1 means respect size hints in tiled resizals */
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
static char dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
static const char *dmenucmd[] = { "dmenu_run", "-m", dmenumon, "-fn", dmenufont, "-nb", col_gray1, "-nf", col_gray3, "-sb", col_cyan, "-sf", col_gray4, NULL };
static const char *termcmd[]  = { "st", NULL };

static const char workspace[] = "/tool/dwm";
static const char *autostartscript = "$DWM/scripts/utils/autostart.sh";

/* commands spawned when clicking statusbar, the mouse button pressed is exported as BUTTON */
static const StatusCmd statuscmds[] = {
  { "notify-send Mouse$BUTTON",           0 },
  { "$DWM/scripts/statusbar/vol.sh",      5 },
  { "$DWM/scripts/statusbar/date.sh",     6 },
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
  { MODKEY,                       XK_F1,     spawn,          SHCMD("$DWM/scripts/statusbar/vol.sh all 0") },
  // Win+F2 音量减小5%
  { MODKEY,                       XK_F2,     spawn,          SHCMD("$DWM/scripts/statusbar/vol.sh all -5") },
  // Win+F3 音量增大5%
  { MODKEY,                       XK_F3,     spawn,          SHCMD("$DWM/scripts/statusbar/vol.sh all +5") },
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

