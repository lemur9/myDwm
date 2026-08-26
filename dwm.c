/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
  * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#define HIDDEN(C)               ((getstate(C->win) == IconicState))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)
#define SYSTEM_TRAY_REQUEST_DOCK    0
/* XEMBED messages */
#define XEMBED_EMBEDDED_NOTIFY      0
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_FOCUS_IN             4
#define XEMBED_MODALITY_ON         10
#define XEMBED_MAPPED              (1 << 0)
#define XEMBED_WINDOW_DEACTIVATE    2
#define VERSION_MAJOR               0
#define VERSION_MINOR               0
#define XEMBED_EMBEDDED_VERSION (VERSION_MAJOR << 16) | VERSION_MINOR
#define OPAQUE                  0xffU

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum {
  SchemeNorm, SchemeSel, SchemeHov, SchemeHid, SchemeUrg,
  SchemeStatSystem, SchemeStatMusic, SchemeStatVolume,
  SchemeStatClock, SchemeStatDate
}; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
  NetSystemTray, NetSystemTrayOP, NetSystemTrayOrientation, NetSystemTrayOrientationHorz,
  NetWMFullscreen, NetActiveWindow, NetWMWindowType,
  NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { Manager, Xembed, XembedInfo, XLast }; /* Xembed atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
  ClkClientWin, ClkRootWin, ClkLast }; /* clicks */
enum { UP, DOWN, LEFT, RIGHT }; /* movewin */
enum { V_EXPAND, V_REDUCE, H_EXPAND, H_REDUCE }; /* resizewins */

typedef union {
  int i;            //整数类型的参数。
  unsigned int ui;  //无符号整数类型的参数。
  float f;          //浮点数类型的参数。
  const void *v;    //指针类型的参数。
} Arg;

typedef struct {
  unsigned int click;             //点击的类型（如标签栏、布局符号、状态文本等）。
  unsigned int mask;              //修饰键掩码（如 Ctrl、Alt 等）。
  unsigned int button;            //鼠标按钮（如鼠标左键、中键、右键等）。
  void (*func)(const Arg *arg);   //当点击时要调用的函数。
  const Arg arg;                  //传递给函数的参数。 
} Button;

/**
 * Monitor:
 *  Monitor 结构体表示一个物理显示器或屏幕。
 *  在多显示器设置中，每个显示器都会有一个对应的 Monitor 对象。
 *  Monitor 对象包含了与显示器相关的所有信息，如显示器的几何位置、当前显示的窗口列表、布局等。
 */
typedef struct Monitor Monitor;

/**
 * Client:
 *  Client 结构体表示一个客户端窗口，即一个被窗口管理器管理的应用程序窗口。
 *  Client 对象包含了与窗口相关的所有信息，如窗口的名称、位置、大小、状态（是否浮动、是否全屏等）、所属的 Monitor 等。
 */
typedef struct Client Client;

/**
 * Window:
 *  Window 是一个 X Window System 中的基本数据类型，表示一个窗口的标识符。
 *  它是一个无符号整数，用于唯一标识一个窗口。Window 类型通常用于与 X11 库进行交互，以执行窗口操作（如创建、销毁、移动、调整大小等）。
 */
struct Client {
  char name[256];                                                         //窗口的名称。
  float mina, maxa;                                                       //窗口的最小和最大宽高比。
  int x, y, w, h;                                                         //窗口的几何位置和尺寸（x 坐标、y 坐标、宽度和高度）。
  int oldx, oldy, oldw, oldh;                                             //窗口的旧几何位置和尺寸。
  int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;       //窗口的基础宽高、增量、最大宽高、最小宽高和提示是否有效。
  int bw, oldbw;                                                          //窗口的边框宽度和旧边框宽度。
  unsigned int tags;                                                      //窗口的标签。
  int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;  //窗口的固定、浮动、紧急、永不聚焦、旧状态和全屏状态。
  Client *next;                                                           //链表形式的下一个客户端。
  Client *snext;                                                          //堆栈中的下一个客户端。
  Monitor *mon;                                                           //窗口所属的显示器。
  Window win;                                                             //窗口的标识符。
};

/**
 * Key:
 *  Key 结构体表示一个键盘快捷键的定义。
 */
typedef struct {
  unsigned int mod;             //修饰键（如 Ctrl、Alt 等）。
  KeySym keysym;                //键盘符号（如 'a', 'b', 'Enter' 等）。
  void (*func)(const Arg *);    //当按下该快捷键时要调用的函数。
  const Arg arg;                //传递给函数的参数。
} Key;

/**
 * Layout:
 *  Layout 结构体表示窗口管理器中的一种布局方式。
 */
typedef struct {
  const char *symbol;             //布局的符号表示（如 "[]=", "><>" 等）。
  void (*arrange)(Monitor *);     //指向一个函数的指针，该函数用于排列窗口。
} Layout;

/**
 * Monitor:
 *  Monitor 结构体表示一个物理显示器或屏幕。
 *  它包含了与显示器相关的所有信息。
 */
struct Monitor {
  char ltsymbol[16];      //布局的符号表示。
  float mfact;            //主区域的大小因子。
  int nmaster;            //主区域的客户端数量。
  int num;                //显示器的编号。
  int by;                 //条形几何形状（状态栏的 y 坐标）。
  int btw;                //栏的任务宽度部分。
  int bt;                 //任务数。
  int mx, my, mw, mh;     //显示器的几何位置和尺寸（x 坐标、y 坐标、宽度和高度）。
  int wx, wy, ww, wh;     //窗口区域的几何位置和尺寸。
  int gappih;             //窗口之间的水平间隙。
  int gappiv;             //窗口之间的垂直间隙。
  int gappoh;             //外部水平间隙。
  int gappov;             //外部垂直间隙。
  unsigned int seltags;   //当前选中的标签。
  unsigned int sellt;     //当前选中的布局。
  unsigned int tagset[2]; //标签集。
  int showbar;            //是否显示状态栏。
  int topbar;             //状态栏是否在顶部.
  int hidsel;             //是否隐藏选中的窗口。
  Client *clients;        //链表形式的客户端窗口列表。
  Client *sel;            //当前选中的客户端窗口。
  Client *hov;            //当前悬停的客户端窗口。
  Client *stack;          //堆栈中的客户端窗口。
  Monitor *next;          //链表形式的下一个显示器。
  Window barwin;          //状态栏窗口。
  const Layout *lt[2];    //当前和上一个布局。
};

/**
 * Rule:
 *  Rule 结构体表示窗口管理器中的规则，用于匹配特定的窗口并应用相应的属性。
 */
typedef struct {
  const char *class;  //窗口的类名。
  const char *instance; //窗口的实例名。
  const char *title;  //窗口的标题。
  unsigned int tags;  //窗口的标签。
  int isfloating; //窗口是否浮动。
  int monitor;  //窗口所属的显示器。
} Rule;

typedef struct {
  const char *cmd;
  int id;
} StatusCmd;

typedef struct {
  unsigned int tags;    /* tag mask, e.g. 1 << 0 */
  const char *cmd;      /* shell command to spawn */
  const char *process;  /* process name for pgrep check; NULL to skip */
} TagCmd;

typedef struct Systray   Systray;
struct Systray {
  Window win;
  Client *icons;
  GC gc;  /* 修复：复用 GC，避免每次 updatesystray 都 XCreateGC 泄漏 */
  unsigned int w;      /* 上次布局的总宽度 */
  unsigned int n;      /* 上次布局的图标数量 */
  int x, y;            /* 上次布局的位置 */
  Monitor *m;          /* 上次布局所在的 monitor */
};

/* function declarations */
static void movewin(const Arg *arg);
static void resizewin(const Arg *arg);
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void attachclient(Client *c);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static Client *bartabclientat(Monitor *m, int px);
static void drawbar(Monitor *m);
static void drawbars(void);
static void drawstatusbar(Monitor *m, int stw);
static unsigned int statusblockwidth(const char *text);
static int statuswidth(char *text);
static int statuscmdat(int px);
static int statusscheme(unsigned char id);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstackvis(const Arg *arg);
static void focusstackhid(const Arg *arg);
static void focusstack(int inc, int vis);
static Atom getatomprop(Client *c, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static unsigned int getsystraywidth();
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void hide(const Arg *arg);
static void hidewin(Client *c);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void pop(Client *c);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void removesystrayicon(Client *i);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizebarwin(Monitor *m);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void resizerequest(XEvent *e);
static void restack(Monitor *m);
static void run(void);
static void runAutostart(void);
static void scan(void);
static int sendevent(Window w, Atom proto, int m, long d0, long d1, long d2, long d3, long d4);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setgaps(int oh, int ov, int ih, int iv);
static void incrgaps(const Arg *arg);
static void incrigaps(const Arg *arg);
static void incrogaps(const Arg *arg);
static void incrohgaps(const Arg *arg);
static void incrovgaps(const Arg *arg);
static void incrihgaps(const Arg *arg);
static void incrivgaps(const Arg *arg);
static void togglegaps(const Arg *arg);
static void defaultgaps(const Arg *arg);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void seturgent(Client *c, int urg);
static void show(const Arg *arg);
static void showall(const Arg *arg);
static void showwin(Client *c);
static void showhide(Client *c);
static void spawn(const Arg *arg);
static Monitor *systraytomon(Monitor *m);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void magicgrid(Monitor *m);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void togglewin(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatesystray(void);
static void updatesystrayicongeom(Client *i, int w, int h);
static void updatesystrayiconstate(Client *i, XPropertyEvent *ev);
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);

static Client *wintosystrayicon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void xinitvisual();
static void zoom(const Arg *arg);

/* variables */
static Systray *systray =  NULL;
static const char broken[] = "broken";
static char stext[256];
static int statusw;
static int statuscmdn;
static char lastbutton[] = "-";
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar height */
static int enablegaps = 1;   /* enables gaps, used by togglegaps */
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
  [ButtonPress] = buttonpress,
  [ClientMessage] = clientmessage,
  [ConfigureRequest] = configurerequest,
  [ConfigureNotify] = configurenotify,
  [DestroyNotify] = destroynotify,
  //  [EnterNotify] = enternotify,
  [Expose] = expose,
  [FocusIn] = focusin,
  [KeyPress] = keypress,
  [MappingNotify] = mappingnotify,
  [MapRequest] = maprequest,
  [MotionNotify] = motionnotify,
  [PropertyNotify] = propertynotify,
  [ResizeRequest] = resizerequest,
  [UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast], xatom[XLast];
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;

static int useargb = 0;
static Visual *visual;
static int depth;
static Colormap cmap;

/* 配置，允许嵌套代码访问上述变量 */
#include "config.h"

/* 性能修复：tag 名称是编译期常量，宽度只算一次。
 * 原来每个 bar 上的 motion 事件都要做 9 次 Xft 文本测量（fontconfig 查找），
 * 高频指针移动时是纯浪费 */
static int tagwcache_done = 0;
static unsigned int tagw[LENGTH(tags)];
static unsigned int
tagwidth(int i)
{
  if (!tagwcache_done) {
    int k;
    for (k = 0; k < LENGTH(tags); k++)
      tagw[k] = MAX(tagwidthpx, TEXTW(tags[k]));
    tagwcache_done = 1;
  }
  return tagw[i];
}

/* 状态文本里的控制字符仍然负责点击分区，同时也选择模块配色。
 * 每个可见分区使用统一的留白，避免状态栏挤成一条连续字符串。 */
static unsigned int
statusblockwidth(const char *text)
{
  return text && *text
    ? drw_fontset_getwidth(drw, text) + 2 * statuspadding
    : 0;
}

static int
statuswidth(char *status)
{
  char *text, *s, ch;
  int blocks = 0, w = 0;
  unsigned int bw;

  for (text = s = status; ; s++) {
    if (!*s || (unsigned char)*s < ' ') {
      ch = *s;
      *s = '\0';
      if ((bw = statusblockwidth(text))) {
        if (blocks++)
          w += statusgap;
        w += bw;
      }
      *s = ch;
      if (!ch)
        break;
      text = s + 1;
    }
  }
  return w;
}

static int
statusscheme(unsigned char id)
{
  switch (id) {
  case 1: return SchemeStatSystem;
  case 4:
  case 5: return SchemeStatMusic;
  case 6: return SchemeStatVolume;
  case 7: return SchemeStatClock;
  case 8: return SchemeStatDate;
  default: return SchemeNorm;
  }
}

/* 返回状态栏相对坐标 px 对应的 statuscmd id。 */
static int
statuscmdat(int px)
{
  char *text, *s, ch;
  int first = 1, x = 0;
  unsigned char id = 0;
  unsigned int bw;

  for (text = s = stext; ; s++) {
    if (!*s || (unsigned char)*s < ' ') {
      ch = *s;
      *s = '\0';
      if ((bw = statusblockwidth(text))) {
        if (!first)
          x += statusgap;
        if (px >= x && px < x + (int)bw) {
          *s = ch;
          return id;
        }
        x += bw;
        first = 0;
      }
      *s = ch;
      if (!ch)
        break;
      id = (unsigned char)ch;
      text = s + 1;
    }
  }
  return 0;
}

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
void
applyrules(Client *c)
{
  const char *class, *instance;
  unsigned int i;
  const Rule *r;
  Monitor *m;
  XClassHint ch = { NULL, NULL };

  /* rule matching */
  c->isfloating = 0;
  c->tags = 0;
  XGetClassHint(dpy, c->win, &ch);
  class    = ch.res_class ? ch.res_class : broken;
  instance = ch.res_name  ? ch.res_name  : broken;

  for (i = 0; i < LENGTH(rules); i++) {
    r = &rules[i];
    if ((!r->title || strstr(c->name, r->title))
      && (!r->class || strstr(class, r->class))
      && (!r->instance || strstr(instance, r->instance)))
    {
      c->isfloating = r->isfloating;
      c->tags |= r->tags;
      for (m = mons; m && m->num != r->monitor; m = m->next);
      if (m)
        c->mon = m;
    }
  }
  if (ch.res_class)
    XFree(ch.res_class);
  if (ch.res_name)
    XFree(ch.res_name);
  c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
 
  if (selmon != c->mon) {
    selmon = c->mon;
    focus(NULL);
  }

  view(&(Arg){ .ui = c-> tags });
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
  int baseismin;
  Monitor *m = c->mon;

  /* set minimum possible */
  *w = MAX(1, *w);
  *h = MAX(1, *h);
  if (interact) {
    if (*x > sw)
      *x = sw - WIDTH(c);
    if (*y > sh)
      *y = sh - HEIGHT(c);
    if (*x + *w + 2 * c->bw < 0)
      *x = 0;
    if (*y + *h + 2 * c->bw < 0)
      *y = 0;
  } else {
    if (*x >= m->wx + m->ww)
      *x = m->wx + m->ww - WIDTH(c);
    if (*y >= m->wy + m->wh)
      *y = m->wy + m->wh - HEIGHT(c);
    if (*x + *w + 2 * c->bw <= m->wx)
      *x = m->wx;
    if (*y + *h + 2 * c->bw <= m->wy)
      *y = m->wy;
  }
  if (*h < bh)
    *h = bh;
  if (*w < bh)
    *w = bh;
  if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
    if (!c->hintsvalid)
      updatesizehints(c);
    /* see last two sentences in ICCCM 4.1.2.3 */
    baseismin = c->basew == c->minw && c->baseh == c->minh;
    if (!baseismin) { /* temporarily remove base dimensions */
      *w -= c->basew;
      *h -= c->baseh;
    }
    /* adjust for aspect limits */
    if (c->mina > 0 && c->maxa > 0) {
      if (c->maxa < (float)*w / *h)
        *w = *h * c->maxa + 0.5;
      else if (c->mina < (float)*h / *w)
        *h = *w * c->mina + 0.5;
    }
    if (baseismin) { /* increment calculation requires this */
      *w -= c->basew;
      *h -= c->baseh;
    }
    /* adjust for increment value */
    if (c->incw)
      *w -= *w % c->incw;
    if (c->inch)
      *h -= *h % c->inch;
    /* restore base dimensions */
    *w = MAX(*w + c->basew, c->minw);
    *h = MAX(*h + c->baseh, c->minh);
    if (c->maxw)
      *w = MIN(*w, c->maxw);
    if (c->maxh)
      *h = MIN(*h, c->maxh);
  }
  return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m)
{
  if (m)
    showhide(m->stack);
  else for (m = mons; m; m = m->next)
    showhide(m->stack);
  if (m) {
    arrangemon(m);
    restack(m);
  } else for (m = mons; m; m = m->next)
    arrangemon(m);
}

void
arrangemon(Monitor *m)
{
  strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
  if (m->lt[m->sellt]->arrange)
    m->lt[m->sellt]->arrange(m);
}

void
attach(Client *c)
{
  if (!newclientathead) {
    Client **tc;
    for (tc = &c->mon->clients; *tc; tc = &(*tc)->next);
    *tc = c;
    c->next = NULL;
  } else {
    attachclient(c);
  }
}

void
attachclient(Client *c)
{
  c->next = c->mon->clients;
  c->mon->clients = c;
}

void
attachstack(Client *c)
{
  c->snext = c->mon->stack;
  c->mon->stack = c;
}

void
buttonpress(XEvent *e)
{
  unsigned int i, x, click, stw = 0;
  Arg arg = {0};
  Client *c;
  Monitor *m;
  XButtonPressedEvent *ev = &e->xbutton;

  click = ClkRootWin;
  /* focus monitor if necessary */
  if ((m = wintomon(ev->window)) && m != selmon) {
    unfocus(selmon->sel, 1);
    selmon = m;
    focus(NULL);
  }
  if (showsystray && m == systraytomon(m))
    stw = getsystraywidth();
  if (ev->window == selmon->barwin) {
    i = x = 0;
    do
    x += tagwidth(i);
    while (ev->x >= x && ++i < LENGTH(tags));
    if (i < LENGTH(tags)) {
      click = ClkTagBar;
      arg.ui = 1 << i;
    } else if (ev->x < x + TEXTW(selmon->ltsymbol))
      click = ClkLtSymbol;
      /* 2px right padding */
    else if (ev->x > selmon->ww - statusw - stw) {
      *lastbutton = '0' + ev->button;
      x = selmon->ww - statusw - stw;
      click = ClkStatusText;
      statuscmdn = statuscmdat(ev->x - x);
    } else {
      c = bartabclientat(m, ev->x);
      if (c) {
        click = ClkWinTitle;
        arg.v = c;
      }
    }
  } else if ((c = wintoclient(ev->window))) {
    focus(c);
    restack(selmon);
    XAllowEvents(dpy, ReplayPointer, CurrentTime);
    click = ClkClientWin;
  }
  for (i = 0; i < LENGTH(buttons); i++)
    if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
      && CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
      buttons[i].func((click == ClkTagBar || click == ClkWinTitle) && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);

  /* spawn tag app if clicking tag bar with Button1 and no client on that tag */
  if (click == ClkTagBar && ev->button == Button1 && CLEANMASK(ev->state) == 0) {
    unsigned int tagmask = arg.ui;
    int has_client = 0;
    Monitor *tm;
    Client *tc;
    for (tm = mons; tm; tm = tm->next)
      for (tc = tm->clients; tc; tc = tc->next)
        if (tc->tags & tagmask) { has_client = 1; break; }
    if (!has_client)
      for (i = 0; i < LENGTH(tagcmds); i++)
        if (tagcmds[i].tags == tagmask && tagcmds[i].cmd) {
          if (tagcmds[i].process) {
            char pgcmd[256];
            snprintf(pgcmd, sizeof(pgcmd), "pgrep -x '%s' > /dev/null 2>&1", tagcmds[i].process);
            if (system(pgcmd) == 0) break; /* process running, skip spawn */
          }
          const char *shcmd[] = { "/bin/sh", "-c", tagcmds[i].cmd, NULL };
          Arg a = { .v = shcmd };
          spawn(&a);
          break;
        }
  }
}

void
checkotherwm(void)
{
  xerrorxlib = XSetErrorHandler(xerrorstart);
  /* this causes an error if some other window manager is running */
  XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
  XSync(dpy, False);
  XSetErrorHandler(xerror);
  XSync(dpy, False);
}

void
cleanup(void)
{
  Arg a = {.ui = ~0};
  Layout foo = { "", NULL };
  Monitor *m;
  size_t i;

  view(&a);
  selmon->lt[selmon->sellt] = &foo;
  for (m = mons; m; m = m->next)
    while (m->stack)
      unmanage(m->stack, 0);
  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  while (mons)
    cleanupmon(mons);
  if (showsystray) {
    XUnmapWindow(dpy, systray->win);
    XDestroyWindow(dpy, systray->win);
    if (systray->gc)
      XFreeGC(dpy, systray->gc);
    free(systray);
  }
  for (i = 0; i < CurLast; i++)
    drw_cur_free(drw, cursor[i]);
  for (i = 0; i < LENGTH(colors); i++)
    free(scheme[i]);
  free(scheme);
  XDestroyWindow(dpy, wmcheckwin);
  drw_free(drw);
  XSync(dpy, False);
  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
  XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
cleanupmon(Monitor *mon)
{
  Monitor *m;

  if (mon == mons)
    mons = mons->next;
  else {
    for (m = mons; m && m->next != mon; m = m->next);
    m->next = mon->next;
  }
  XUnmapWindow(dpy, mon->barwin);
  XDestroyWindow(dpy, mon->barwin);
  free(mon);
}

void
clientmessage(XEvent *e)
{
  XWindowAttributes wa;
  XSetWindowAttributes swa;
  XClientMessageEvent *cme = &e->xclient;
  Client *c = wintoclient(cme->window);

  if (showsystray && cme->window == systray->win && cme->message_type == netatom[NetSystemTrayOP]) {
    /* add systray icons */
    if (cme->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
      if (!(c = (Client *)calloc(1, sizeof(Client))))
        die("fatal: could not malloc() %u bytes\n", sizeof(Client));
      if (!(c->win = cme->data.l[2])) {
        free(c);
        return;
      }
      c->mon = selmon;
      c->next = systray->icons;
      systray->icons = c;
      XGetWindowAttributes(dpy, c->win, &wa);
      c->x = c->oldx = c->y = c->oldy = 0;
      c->w = c->oldw = wa.width;
      c->h = c->oldh = wa.height;
      c->oldbw = wa.border_width;
      c->bw = 0;
      c->isfloating = True;
      /* reuse tags field as mapped status */
      c->tags = 1;
      updatesizehints(c);
      updatesystrayicongeom(c, wa.width, wa.height);
      XAddToSaveSet(dpy, c->win);
      XSelectInput(dpy, c->win, StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);
      XReparentWindow(dpy, c->win, systray->win, 0, 0);
      /* use parents background color */
      swa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
      XChangeWindowAttributes(dpy, c->win, CWBackPixel, &swa);
      sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_EMBEDDED_NOTIFY, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
      /* FIXME not sure if I have to send these events, too */
      sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_FOCUS_IN, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
      sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
      sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_MODALITY_ON, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
      XSync(dpy, False);
      resizebarwin(selmon);
      updatesystray();
      setclientstate(c, NormalState);
    }
    return;
  }

  if (!c)
    return;
  if (cme->message_type == netatom[NetWMState]) {
    if (cme->data.l[1] == netatom[NetWMFullscreen]
      || cme->data.l[2] == netatom[NetWMFullscreen])
      setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                    || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
  } else if (cme->message_type == netatom[NetActiveWindow]) {
    if (c != selmon->sel && !c->isurgent)
      seturgent(c, 1);
  }
}

void
configure(Client *c)
{
  XConfigureEvent ce;

  ce.type = ConfigureNotify;
  ce.display = dpy;
  ce.event = c->win;
  ce.window = c->win;
  ce.x = c->x;
  ce.y = c->y;
  ce.width = c->w;
  ce.height = c->h;
  ce.border_width = c->bw;
  ce.above = None;
  ce.override_redirect = False;
  XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e)
{
  Monitor *m;
  Client *c;
  XConfigureEvent *ev = &e->xconfigure;
  int dirty;

  /* TODO: updategeom handling sucks, needs to be simplified */
  if (ev->window == root) {
    dirty = (sw != ev->width || sh != ev->height);
    sw = ev->width;
    sh = ev->height;
    if (updategeom() || dirty) {
      drw_resize(drw, sw, bh);
      updatebars();
      for (m = mons; m; m = m->next) {
        for (c = m->clients; c; c = c->next)
          if (c->isfullscreen)
            resizeclient(c, m->mx, m->my, m->mw, m->mh);
        resizebarwin(m);
      }
      focus(NULL);
      arrange(NULL);
    }
  }
}

void
configurerequest(XEvent *e)
{
  Client *c;
  Monitor *m;
  XConfigureRequestEvent *ev = &e->xconfigurerequest;
  XWindowChanges wc;

  if ((c = wintoclient(ev->window))) {
    if (ev->value_mask & CWBorderWidth)
      c->bw = ev->border_width;
    else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
      m = c->mon;
      if (ev->value_mask & CWX) {
        c->oldx = c->x;
        c->x = m->mx + ev->x;
      }
      if (ev->value_mask & CWY) {
        c->oldy = c->y;
        c->y = m->my + ev->y;
      }
      if (ev->value_mask & CWWidth) {
        c->oldw = c->w;
        c->w = ev->width;
      }
      if (ev->value_mask & CWHeight) {
        c->oldh = c->h;
        c->h = ev->height;
      }
      if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
        c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
      if ((c->y + c->h) > m->my + m->mh && c->isfloating)
        c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
      if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
        configure(c);
      if (ISVISIBLE(c))
        XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    } else
    configure(c);
  } else {
    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
  }
  XSync(dpy, False);
}

Monitor *
createmon(void)
{
  Monitor *m;

  m = ecalloc(1, sizeof(Monitor));
  m->tagset[0] = m->tagset[1] = 1;
  m->mfact = mfact;
  m->nmaster = nmaster;
  m->showbar = showbar;
  m->topbar = topbar;
  m->gappih = gappih;
  m->gappiv = gappiv;
  m->gappoh = gappoh;
  m->gappov = gappov;
  m->lt[0] = &layouts[0];
  m->lt[1] = &layouts[1 % LENGTH(layouts)];
  strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
  return m;
}

void
destroynotify(XEvent *e)
{
  Client *c;
  XDestroyWindowEvent *ev = &e->xdestroywindow;

  if ((c = wintoclient(ev->window)))
    unmanage(c, 1);
  else if ((c = wintosystrayicon(ev->window))) {
    removesystrayicon(c);
    resizebarwin(selmon);
    updatesystray();
  }
}

void
detach(Client *c)
{
  Client **tc;

  for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
  *tc = c->next;
}

void
detachstack(Client *c)
{
  Client **tc, *t;

  for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
  *tc = c->snext;

  if (c == c->mon->sel) {
    for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
    c->mon->sel = t;
  }
}

Monitor *
dirtomon(int dir)
{
  Monitor *m = NULL;

  if (dir > 0) {
    if (!(m = selmon->next))
      m = mons;
  } else if (selmon == mons)
    for (m = mons; m->next; m = m->next);
  else
    for (m = mons; m->next != selmon; m = m->next);
  return m;
}

/* 计算客户端 c 在 awesomebar 标题区的位置和宽度（与 drawbar 的几何逻辑一致） */
static int
bartabgeom(Monitor *m, Client *target, int *xout, int *wout)
{
  int x, w, tw = 0, stw = 0, n = 0, i;
  unsigned int q, r;
  Client *c;

  if (!m->showbar)
    return 0;
  if (showsystray && m == systraytomon(m))
    stw = getsystraywidth();
  if (m == selmon)
    tw = statusw;

  for (c = m->clients; c; c = c->next)
    if (ISVISIBLE(c))
      n++;
  if (n == 0)
    return 0;

  x = 0;
  for (i = 0; i < LENGTH(tags); i++)
    x += tagwidth(i);
  x += TEXTW(m->ltsymbol);

  w = m->ww - tw - stw - x;
  if (w <= (int)bh)
    return 0;

  q = w / n;      /* 基本 tab 宽度 */
  r = w % n;      /* 前 r 个 tab 各多 1px */
  i = 0;
  for (c = m->clients; c; c = c->next) {
    if (!ISVISIBLE(c))
      continue;
    if (c == target) {
      *xout = x;
      *wout = (i < (int)r) ? q + 1 : q;
      return 1;
    }
    x += (i < (int)r) ? q + 1 : q;
    i++;
  }
  return 0;
}

static Client *
bartabclientat(Monitor *m, int px)
{
  int x, w;
  Client *c;

  for (c = m->clients; c; c = c->next) {
    if (!ISVISIBLE(c))
      continue;
    if (bartabgeom(m, c, &x, &w) && px >= x && px < x + w)
      return c;
  }
  return NULL;
}

/* 画单个窗口的标题 tab；docopy 为真时立即把该区域拷贝到 barwin（局部刷新） */
static void
drawbartab(Monitor *m, Client *c, int docopy)
{
  int x, w, scm;

  if (!bartabgeom(m, c, &x, &w))
    return;
  if (c->isurgent)
    scm = SchemeUrg;
  else if (m->hov == c)
    scm = SchemeHov;
  else if (m->sel == c)
    scm = SchemeSel;
  else if (HIDDEN(c))
    scm = SchemeHid;
  else
    scm = SchemeNorm;
  drw_setscheme(drw, scheme[scm]);
  drw_text(drw, x, 0, w, bh, lrpad / 2, c->name, 0);

  /* 选中和紧急窗口用细底线表达状态，避免整块高饱和背景。 */
  if (m->sel == c || c->isurgent) {
    int inset = MIN(lrpad / 2, MAX(0, (w - 1) / 2));
    drw_rect(drw, x + inset, bh - tagindicator,
             MAX(1, w - 2 * inset), tagindicator, 1, 0);
  }

  if (docopy) {
    XCopyArea(drw->dpy, drw->drawable, m->barwin, drw->gc, x, 0, w, bh, x, 0);
    XFlush(drw->dpy);
  }
}

static void
drawstatusbar(Monitor *m, int stw)
{
  char *text, *s, ch;
  int first = 1;
  int x = m->ww - statusw - stw;
  unsigned char id = 0;
  unsigned int bw;

  /* 先清空模块间隔，防止短文本覆盖不掉上一帧。 */
  drw_setscheme(drw, scheme[SchemeNorm]);
  drw_rect(drw, x, 0, statusw, bh, 1, 1);

  for (text = s = stext; ; s++) {
    if (!*s || (unsigned char)*s < ' ') {
      ch = *s;
      *s = '\0';
      if ((bw = statusblockwidth(text))) {
        if (!first)
          x += statusgap;
        drw_setscheme(drw, scheme[statusscheme(id)]);
        drw_text(drw, x, 0, bw, bh, statuspadding, text, 0);
        x += bw;
        first = 0;
      }
      *s = ch;
      if (!ch)
        break;
      id = (unsigned char)ch;
      text = s + 1;
    }
  }
}

void
drawbar(Monitor *m)
{
  int x, w, tw = 0, stw = 0, n = 0;
  unsigned int i, occ = 0, urg = 0;
  Client *c;

  if(showsystray && m == systraytomon(m))
    stw = getsystraywidth();

  if (!m->showbar)
    return;

  /* 状态栏只画在当前显示器；模块间隔与着色由 drawstatusbar 统一处理。 */
  if (m == selmon) {
    drawstatusbar(m, stw);
    tw = statusw;
  }

  resizebarwin(m);
  for (c = m->clients; c; c = c->next) {
    if (ISVISIBLE(c))
      n++;
    occ |= c->tags;
    if (c->isurgent)
      urg |= c->tags;
  }
  x = 0;
  for (i = 0; i < LENGTH(tags); i++) {
    int selected = m->tagset[m->seltags] & 1 << i;
    int urgent = urg & 1 << i;
    int scm = urgent ? SchemeUrg : selected ? SchemeSel : SchemeNorm;

    w = tagwidth(i);
    drw_setscheme(drw, scheme[scm]);
    drw_text(drw, x, 0, w, bh, (w - drw_fontset_getwidth(drw, tags[i])) / 2,
             tags[i], 0);

    if (selected || urgent) {
      drw_rect(drw, x + 9, bh - tagindicator,
               MAX(1, w - 18), tagindicator, 1, 0);
    } else if (occ & 1 << i) {
      /* 未选中但有窗口：一个克制的小圆点。 */
      drw_ellipse(drw, x + w / 2 - 2, bh - 6, 4, 4, 1, 0);
    }
    x += w;
  }
  w = TEXTW(m->ltsymbol);
  drw_setscheme(drw, scheme[SchemeNorm]);
  x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);

  if ((w = m->ww - tw - stw - x) > bh) {
    if (n > 0) {
      for (c = m->clients; c; c = c->next)
        if (ISVISIBLE(c))
          drawbartab(m, c, 0);
    } else {
      drw_setscheme(drw, scheme[SchemeNorm]);
      drw_rect(drw, x, 0, w, bh, 1, 1);
    }
  }
  m->bt = n;
  m->btw = w;
  drw_map(drw, m->barwin, 0, 0, m->ww - stw, bh);
}

void
drawbars(void)
{
  Monitor *m;

  for (m = mons; m; m = m->next)
    drawbar(m);
  if (showsystray)
    updatesystray();
}

/**
 * enternotify 函数处理 XEvent 类型的事件，特别是 XCrossingEvent 事件。
 * XCrossingEvent 事件在鼠标指针进入或离开窗口时触发。
 */
void
enternotify(XEvent *e)
{
  Client *c;
  Monitor *m;

  // ev 被初始化为传入事件的 xcrossing 成员
  XCrossingEvent *ev = &e->xcrossing;

  // 检查事件的模式和细节。
  // 如果事件模式不是 NotifyNormal 或者事件细节是 NotifyInferior，并且事件窗口不是根窗口，则函数直接返回，不做任何处理。
  if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
    return;

  // wintoclient函数 允许通过窗口标识符找到与之关联的客户端对象
  // 将事件窗口转换为 Client 对象。如果转换成功，c 将指向对应的 Client 对象，否则为 NULL。
  c = wintoclient(ev->window);
  // 函数通过三元运算符判断 c 是否为 NULL，
  // 如果不是，则 m 被设置为 c 所在的 Monitor，否则调用 wintomon 函数将事件窗口转换为 Monitor 对象。
  m = c ? c->mon : wintomon(ev->window);

  // 函数检查当前 Monitor 是否与全局变量 selmon 指向的 Monitor 不同。
  // 如果不同，则调用 unfocus 函数取消当前选中窗口的焦点，并将 selmon 设置为新的 Monitor。
  // 如果 Monitor 相同，并且 Client 对象为 NULL 或者与当前选中的 Client 相同，则函数直接返回。
  if (m != selmon) {
    unfocus(selmon->sel, 1);
    selmon = m;
  } else if (!c || c == selmon->sel)
    return;

  // 如果上述条件都不满足，函数调用 focus 函数将焦点设置到新的 Client 对象上。
  focus(c);
}

void
expose(XEvent *e)
{
  Monitor *m;
  XExposeEvent *ev = &e->xexpose;

  if (ev->count == 0 && (m = wintomon(ev->window))) {
    drawbar(m);
    if (m == selmon)
      updatesystray();
  }
}

void
focus(Client *c)
{
  if (!c || !ISVISIBLE(c))
    for (c = selmon->stack; c && (!ISVISIBLE(c) || HIDDEN(c)); c = c->snext);
  if (selmon->sel && selmon->sel != c) {
    unfocus(selmon->sel, 0);

    if (selmon->hidsel) {
      hidewin(selmon->sel);
      if (c)
        arrange(c->mon);
      selmon->hidsel = 0;
    }
  }
  if (c) {
    if (c->mon != selmon)
      selmon = c->mon;
    if (c->isurgent)
      seturgent(c, 0);
    detachstack(c);
    attachstack(c);
    grabbuttons(c, 1);
    XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
    setfocus(c);
  } else {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
  selmon->sel = c;
  drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
  XFocusChangeEvent *ev = &e->xfocus;

  if (selmon->sel && ev->window != selmon->sel->win)
    setfocus(selmon->sel);
}

void
focusmon(const Arg *arg)
{
  Monitor *m;

  if (!mons->next)
    return;
  if ((m = dirtomon(arg->i)) == selmon)
    return;
  unfocus(selmon->sel, 0);
  selmon = m;
  focus(NULL);
}

void
focusstackvis(const Arg *arg) {
  focusstack(arg->i, 0);
}

void
focusstackhid(const Arg *arg) {
  focusstack(arg->i, 1);
}

void
focusstack(int inc, int hid)
{
  Client *c = NULL, *i;
  // if no client selected AND exclude hidden client; if client selected but fullscreened
  if ((!selmon->sel && !hid) || (selmon->sel && selmon->sel->isfullscreen && lockfullscreen))
    return;
  if (!selmon->clients)
    return;
  if (inc > 0) {
    if (selmon->sel)
      for (c = selmon->sel->next;
        c && (!ISVISIBLE(c) || (!hid && HIDDEN(c)));
        c = c->next);
    if (!c)
      for (c = selmon->clients;
        c && (!ISVISIBLE(c) || (!hid && HIDDEN(c)));
        c = c->next);
  } else {
    if (selmon->sel) {
      for (i = selmon->clients; i != selmon->sel; i = i->next)
        if (ISVISIBLE(i) && !(!hid && HIDDEN(i)))
          c = i;
    } else
    c = selmon->clients;
    if (!c)
      for (; i; i = i->next)
        if (ISVISIBLE(i) && !(!hid && HIDDEN(i)))
          c = i;
  }
  if (c) {
    focus(c);
    restack(selmon);
    if (HIDDEN(c)) {
      showwin(c);
      c->mon->hidsel = 1;
    }
  }
}

Atom
getatomprop(Client *c, Atom prop)
{
  int di;
  unsigned long dl;
  unsigned char *p = NULL;
  Atom da, atom = None;
  /* FIXME getatomprop should return the number of items and a pointer to
   * the stored data instead of this workaround */
  Atom req = XA_ATOM;
  if (prop == xatom[XembedInfo])
    req = xatom[XembedInfo];

  if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, req,
                         &da, &di, &dl, &dl, &p) == Success && p) {
    atom = *(Atom *)p;
    if (da == xatom[XembedInfo] && dl == 2)
      atom = ((Atom *)p)[1];
    XFree(p);
  }
  return atom;
}

int
getrootptr(int *x, int *y)
{
  int di;
  unsigned int dui;
  Window dummy;

  return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
  int format;
  long result = -1;
  unsigned char *p = NULL;
  unsigned long n, extra;
  Atom real;

  if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
                         &real, &format, &n, &extra, (unsigned char **)&p) != Success)
    return -1;
  if (n != 0)
    result = *p;
  XFree(p);
  return result;
}

unsigned int
getsystraywidth()
{
  unsigned int w = 0;
  Client *i;
  if(showsystray)
    for(i = systray->icons; i; w += i->w + systrayspacing, i = i->next) ;
  return w ? w + systrayspacing : 1;
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
  char **list = NULL;
  int n;
  XTextProperty name;

  if (!text || size == 0)
    return 0;
  text[0] = '\0';
  if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
    return 0;
  if (name.encoding == XA_STRING) {
    strncpy(text, (char *)name.value, size - 1);
  } else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
    strncpy(text, *list, size - 1);
    XFreeStringList(list);
  }
  text[size - 1] = '\0';
  XFree(name.value);
  return 1;
}

void
grabbuttons(Client *c, int focused)
{
  updatenumlockmask();
  {
    unsigned int i, j;
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    if (!focused)
      XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
                  BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
    for (i = 0; i < LENGTH(buttons); i++)
      if (buttons[i].click == ClkClientWin)
        for (j = 0; j < LENGTH(modifiers); j++)
          XGrabButton(dpy, buttons[i].button,
                      buttons[i].mask | modifiers[j],
                      c->win, False, BUTTONMASK,
                      GrabModeAsync, GrabModeSync, None, None);
  }
}

void
grabkeys(void)
{
  updatenumlockmask();
  {
    unsigned int i, j, k;
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    int start, end, skip;
    KeySym *syms;

    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    XDisplayKeycodes(dpy, &start, &end);
    syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
    if (!syms)
      return;
    for (k = start; k <= end; k++)
      for (i = 0; i < LENGTH(keys); i++)
        /* skip modifier codes, we do that ourselves */
        if (keys[i].keysym == syms[(k - start) * skip])
          for (j = 0; j < LENGTH(modifiers); j++)
            XGrabKey(dpy, k,
                     keys[i].mod | modifiers[j],
                     root, True,
                     GrabModeAsync, GrabModeAsync);
    XFree(syms);
  }
}

void
hide(const Arg *arg)
{
  hidewin(selmon->sel);
  focus(NULL);
  arrange(selmon);
}

void
hidewin(Client *c) {
  if (!c || HIDDEN(c))
    return;

  Window w = c->win;
  static XWindowAttributes ra, ca;

  // more or less taken directly from blackbox's hide() function
  XGrabServer(dpy);
  XGetWindowAttributes(dpy, root, &ra);
  XGetWindowAttributes(dpy, w, &ca);
  // prevent UnmapNotify events
  XSelectInput(dpy, root, ra.your_event_mask & ~SubstructureNotifyMask);
  XSelectInput(dpy, w, ca.your_event_mask & ~StructureNotifyMask);
  XUnmapWindow(dpy, w);
  setclientstate(c, IconicState);
  XSelectInput(dpy, root, ra.your_event_mask);
  XSelectInput(dpy, w, ca.your_event_mask);
  XUngrabServer(dpy);
}

void
incnmaster(const Arg *arg)
{
  selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
  arrange(selmon);
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
  while (n--)
    if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
      && unique[n].width == info->width && unique[n].height == info->height)
      return 0;
  return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent *e)
{
  unsigned int i;
  KeySym keysym;
  XKeyEvent *ev;

  ev = &e->xkey;
  keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
  for (i = 0; i < LENGTH(keys); i++)
    if (keysym == keys[i].keysym
      && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
      && keys[i].func)
      keys[i].func(&(keys[i].arg));
}

void
killclient(const Arg *arg)
{
  if (!selmon->sel)
    return;
  if (!sendevent(selmon->sel->win, wmatom[WMDelete], NoEventMask, wmatom[WMDelete], CurrentTime, 0 , 0, 0)) {
    XGrabServer(dpy);
    XSetErrorHandler(xerrordummy);
    XSetCloseDownMode(dpy, DestroyAll);
    XKillClient(dpy, selmon->sel->win);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
}

void
manage(Window w, XWindowAttributes *wa)
{
  Client *c, *t = NULL;
  Window trans = None;
  XWindowChanges wc;

  c = ecalloc(1, sizeof(Client));
  c->win = w;
  /* geometry */
  c->x = c->oldx = wa->x;
  c->y = c->oldy = wa->y;
  c->w = c->oldw = wa->width;
  c->h = c->oldh = wa->height;
  c->oldbw = wa->border_width;

  updatetitle(c);
  if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
    c->mon = t->mon;
    c->tags = t->tags;
  } else {
    c->mon = selmon;
    applyrules(c);
  }

  if (c->x + WIDTH(c) > c->mon->wx + c->mon->ww)
    c->x = c->mon->wx + c->mon->ww - WIDTH(c);
  if (c->y + HEIGHT(c) > c->mon->wy + c->mon->wh)
    c->y = c->mon->wy + c->mon->wh - HEIGHT(c);
  c->x = MAX(c->x, c->mon->wx);
  c->y = MAX(c->y, c->mon->wy);
  c->bw = borderpx;

  wc.border_width = c->bw;
  XConfigureWindow(dpy, w, CWBorderWidth, &wc);
  XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
  configure(c); /* propagates border_width, if size doesn't change */
  updatewindowtype(c);
  updatesizehints(c);
  updatewmhints(c);
  XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
  grabbuttons(c, 0);
  if (!c->isfloating)
    c->isfloating = c->oldstate = trans != None || c->isfixed;
  if (c->isfloating)
    XRaiseWindow(dpy, c->win);
  attach(c);
  attachstack(c);
  XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
                  (unsigned char *) &(c->win), 1);
  XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
  if (!HIDDEN(c))
    setclientstate(c, NormalState);
  if (c->mon == selmon)
    unfocus(selmon->sel, 0);
  c->mon->sel = c;
  arrange(c->mon);
  if (!HIDDEN(c))
    XMapWindow(dpy, c->win);
  focus(NULL);
}

void
mappingnotify(XEvent *e)
{
  XMappingEvent *ev = &e->xmapping;

  XRefreshKeyboardMapping(ev);
  if (ev->request == MappingKeyboard)
    grabkeys();
}

void
maprequest(XEvent *e)
{
  static XWindowAttributes wa;
  XMapRequestEvent *ev = &e->xmaprequest;
  Client *i;
  if ((i = wintosystrayicon(ev->window))) {
    sendevent(i->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0, systray->win, XEMBED_EMBEDDED_VERSION);
    resizebarwin(selmon);
    updatesystray();
  }

  if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
    return;
  if (!wintoclient(ev->window))
    manage(ev->window, &wa);
}

void
monocle(Monitor *m)
{
  unsigned int n = 0;
  Client *c;

  for (c = m->clients; c; c = c->next)
    if (ISVISIBLE(c))
      n++;
  if (n > 0) /* override layout symbol */
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
  for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
    resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
}

void
motionnotify(XEvent *e)
{
  int x, i;
  // static Monitor *mon = NULL;
  Client *c;
  Monitor *m;
  XMotionEvent *ev = &e->xmotion;

  if (ev->window != selmon->barwin) {
    if (selmon->hov) {
      if (selmon->hov != selmon->sel)
        XSetWindowBorder(dpy, selmon->hov->win, scheme[SchemeNorm][ColBorder].pixel);
      else
        XSetWindowBorder(dpy, selmon->hov->win, scheme[SchemeSel][ColBorder].pixel);

      selmon->hov = NULL;
      c = wintoclient(ev->window);
      m = c ? c->mon : wintomon(ev->window);
      drawbar(m);
    }
    /*
    if (ev->window == root) {
      if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
        unfocus(selmon->sel, 1);
        selmon = m;
        focus(NULL);
      }
      mon = m;
    }
*/
    return;
  }

  c = wintoclient(ev->window);
  m = c ? c->mon : wintomon(ev->window);
  c = m->clients;

  x = 0, i = 0;
  do
  x += tagwidth(i);
  while (ev->x >= x && ++i < LENGTH(tags));
  if (i < LENGTH(tags) || ev->x < x + TEXTW(selmon->ltsymbol) || ev->x > selmon->ww - TEXTW(stext) + lrpad - 2) {
    if (selmon->hov) {
      if (selmon->hov != selmon->sel)
        XSetWindowBorder(dpy, selmon->hov->win, scheme[SchemeNorm][ColBorder].pixel);
      else
        XSetWindowBorder(dpy, selmon->hov->win, scheme[SchemeSel][ColBorder].pixel);
      selmon->hov = NULL;
      drawbar(m);
    }
  } else {
    if (c) {
      Client *nhov = bartabclientat(m, ev->x);
      /* 性能修复：只在 hover 目标真正变化时才重绘 bar。
       * 之前每次指针移动事件都全量重绘（标题 Xft 渲染 + XCopyArea + XSync 往返），
       * 鼠标在 bar 上移动时会产生重绘风暴，拖慢整个桌面 */
      if (nhov != selmon->hov) {
        Client *old = selmon->hov;
        if (old) {
          if (old != selmon->sel)
            XSetWindowBorder(dpy, old->win, scheme[SchemeNorm][ColBorder].pixel);
          else
            XSetWindowBorder(dpy, old->win, scheme[SchemeSel][ColBorder].pixel);
        }
        selmon->hov = nhov;
        if (nhov)
          XSetWindowBorder(dpy, nhov->win, scheme[SchemeHov][ColBorder].pixel);
        /* 性能修复：hover 变化只局部重绘新旧两个 tab（每个仅 1 个标题），
         * 不再全量 drawbar。窗口多时全量重绘 × 扫掠 = 秒级卡顿 */
        if (old)
          drawbartab(m, old, 1);
        if (nhov)
          drawbartab(m, nhov, 1);
      }
    }
  }
}

void
movemouse(const Arg *arg)
{
  int x, y, ocx, ocy, nx, ny;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;

  if (!(c = selmon->sel))
    return;
  if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
    return;
  restack(selmon);
  ocx = c->x;
  ocy = c->y;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                   None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
    return;
  if (!getrootptr(&x, &y))
    return;
  do {
    XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
    switch(ev.type) {
      case ConfigureRequest:
      case Expose:
      case MapRequest:
        handler[ev.type](&ev);
        break;
      case MotionNotify:
        if ((ev.xmotion.time - lasttime) <= (1000 / 60))
          continue;
        lasttime = ev.xmotion.time;

        nx = ocx + (ev.xmotion.x - x);
        ny = ocy + (ev.xmotion.y - y);
        if (abs(selmon->wx - nx) < snap)
          nx = selmon->wx;
        else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
          nx = selmon->wx + selmon->ww - WIDTH(c);
        if (abs(selmon->wy - ny) < snap)
          ny = selmon->wy;
        else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
          ny = selmon->wy + selmon->wh - HEIGHT(c);
        if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
          && (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
          togglefloating(NULL);
        if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
          resize(c, nx, ny, c->w, c->h, 1);
        break;
    }
  } while (ev.type != ButtonRelease);
  XUngrabPointer(dpy, CurrentTime);
  if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }
}

Client *
nexttiled(Client *c)
{
  for (; c && (c->isfloating || !ISVISIBLE(c) || HIDDEN(c)); c = c->next);
  return c;
}

void
pop(Client *c)
{
  detach(c);
  attachclient(c);
  focus(c);
  arrange(c->mon);
}

void
propertynotify(XEvent *e)
{
  Client *c;
  Window trans;
  XPropertyEvent *ev = &e->xproperty;

  if ((c = wintosystrayicon(ev->window))) {
    if (ev->atom == XA_WM_NORMAL_HINTS) {
      updatesizehints(c);
      updatesystrayicongeom(c, c->w, c->h);
    }
    else
    updatesystrayiconstate(c, ev);
    resizebarwin(selmon);
    updatesystray();
  }
  if ((ev->window == root) && (ev->atom == XA_WM_NAME))
    updatestatus();
  else if (ev->state == PropertyDelete)
    return; /* ignore */
  else if ((c = wintoclient(ev->window))) {
    switch(ev->atom) {
      default: break;
      case XA_WM_TRANSIENT_FOR:
        if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
          (c->isfloating = (wintoclient(trans)) != NULL))
          arrange(c->mon);
        break;
      case XA_WM_NORMAL_HINTS:
        c->hintsvalid = 0;
        break;
      case XA_WM_HINTS:
        updatewmhints(c);
        drawbars();
        break;
    }
    if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
      updatetitle(c);
      if (c == c->mon->sel)
        drawbar(c->mon);
    }
    if (ev->atom == netatom[NetWMWindowType])
      updatewindowtype(c);
  }
}

void
quit(const Arg *arg)
{
  // fix: reloading dwm keeps all the hidden clients hidden
  Monitor *m;
  Client *c;
  for (m = mons; m; m = m->next) {
    if (m) {
      for (c = m->stack; c; c = c->next)
        if (c && HIDDEN(c)) showwin(c);
    }
  }

  running = 0;
}

Monitor *
recttomon(int x, int y, int w, int h)
{
  Monitor *m, *r = selmon;
  int a, area = 0;

  for (m = mons; m; m = m->next)
    if ((a = INTERSECT(x, y, w, h, m)) > area) {
      area = a;
      r = m;
    }
  return r;
}

void
removesystrayicon(Client *i)
{
  Client **ii;

  if (!showsystray || !i)
    return;
  for (ii = &systray->icons; *ii && *ii != i; ii = &(*ii)->next);
  if (ii)
    *ii = i->next;
  free(i);
}


void
resize(Client *c, int x, int y, int w, int h, int interact)
{
  if (applysizehints(c, &x, &y, &w, &h, interact))
    resizeclient(c, x, y, w, h);
}

void
resizebarwin(Monitor *m) {
  unsigned int w = m->ww;
  if (showsystray && m == systraytomon(m))
    w -= getsystraywidth();
  XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, w, bh);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
  XWindowChanges wc;

  c->oldx = c->x; c->x = wc.x = x;
  c->oldy = c->y; c->y = wc.y = y;
  c->oldw = c->w; c->w = wc.width = w;
  c->oldh = c->h; c->h = wc.height = h;
  wc.border_width = c->bw;
  XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
  configure(c);
  XSync(dpy, False);
}

void
resizemouse(const Arg *arg)
{
  int ocx, ocy, nw, nh;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;

  if (!(c = selmon->sel))
    return;
  if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
    return;
  restack(selmon);
  ocx = c->x;
  ocy = c->y;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                   None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
    return;
  XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
  do {
    XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
    switch(ev.type) {
      case ConfigureRequest:
      case Expose:
      case MapRequest:
        handler[ev.type](&ev);
        break;
      case MotionNotify:
        if ((ev.xmotion.time - lasttime) <= (1000 / 60))
          continue;
        lasttime = ev.xmotion.time;

        nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
        nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
        if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
          && c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh)
        {
          if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
            && (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
            togglefloating(NULL);
        }
        if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
          resize(c, c->x, c->y, nw, nh, 1);
        break;
    }
  } while (ev.type != ButtonRelease);
  XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
  XUngrabPointer(dpy, CurrentTime);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
  if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
    sendmon(c, m);
    selmon = m;
    focus(NULL);
  }
}

void
resizerequest(XEvent *e)
{
  XResizeRequestEvent *ev = &e->xresizerequest;
  Client *i;

  if ((i = wintosystrayicon(ev->window))) {
    updatesystrayicongeom(i, ev->width, ev->height);
    resizebarwin(selmon);
    updatesystray();
  }
}

void
restack(Monitor *m)
{
  Client *c;
  XEvent ev;
  XWindowChanges wc;

  drawbar(m);
  if (!m->sel)
    return;
  if (m->sel->isfloating || !m->lt[m->sellt]->arrange)
    XRaiseWindow(dpy, m->sel->win);
  if (m->lt[m->sellt]->arrange) {
    wc.stack_mode = Below;
    wc.sibling = m->barwin;
    for (c = m->stack; c; c = c->snext)
      if (!c->isfloating && ISVISIBLE(c)) {
        XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
        wc.sibling = c->win;
      }
  }
  XSync(dpy, False);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void)
{
  XEvent ev;
  /* main event loop */
  XSync(dpy, False);
  while (running && !XNextEvent(dpy, &ev))
    if (handler[ev.type])
      handler[ev.type](&ev); /* call handler */
}

void
runAutostart()
{
  /* load env */
  setenv("DWM", workspace, 1);

  char cmd [100];
  sprintf(cmd, "%s &", autostartscript);
  system(cmd);
}

void
scan(void)
{
  unsigned int i, num;
  Window d1, d2, *wins = NULL;
  XWindowAttributes wa;

  if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
    for (i = 0; i < num; i++) {
      if (!XGetWindowAttributes(dpy, wins[i], &wa)
        || wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
        continue;
      if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
        manage(wins[i], &wa);
    }
    for (i = 0; i < num; i++) { /* now the transients */
      if (!XGetWindowAttributes(dpy, wins[i], &wa))
        continue;
      if (XGetTransientForHint(dpy, wins[i], &d1)
        && (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
        manage(wins[i], &wa);
    }
    if (wins)
      XFree(wins);
  }
}

void
sendmon(Client *c, Monitor *m)
{
  if (c->mon == m)
    return;
  unfocus(c, 1);
  detach(c);
  detachstack(c);
  c->mon = m;
  c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
  attach(c);
  attachstack(c);
  focus(NULL);
  arrange(NULL);
}

void
setclientstate(Client *c, long state)
{
  long data[] = { state, None };

  XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
                  PropModeReplace, (unsigned char *)data, 2);
}

int
sendevent(Window w, Atom proto, int mask, long d0, long d1, long d2, long d3, long d4)
{
  int n;
  Atom *protocols, mt;
  int exists = 0;
  XEvent ev;

  if (proto == wmatom[WMTakeFocus] || proto == wmatom[WMDelete]) {
    mt = wmatom[WMProtocols];
    if (XGetWMProtocols(dpy, w, &protocols, &n)) {
      while (!exists && n--)
        exists = protocols[n] == proto;
      XFree(protocols);
    }
  }
  else {
    exists = True;
    mt = proto;
  }
  if (exists) {
    ev.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = mt;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = d0;
    ev.xclient.data.l[1] = d1;
    ev.xclient.data.l[2] = d2;
    ev.xclient.data.l[3] = d3;
    ev.xclient.data.l[4] = d4;
    XSendEvent(dpy, w, False, mask, &ev);
  }
  return exists;
}

void
setfocus(Client *c)
{
  if (!c->neverfocus) {
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    XChangeProperty(dpy, root, netatom[NetActiveWindow],
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *) &(c->win), 1);
  }
  sendevent(c->win, wmatom[WMTakeFocus], NoEventMask, wmatom[WMTakeFocus], CurrentTime, 0, 0, 0);
}

void
setfullscreen(Client *c, int fullscreen)
{
  if (fullscreen && !c->isfullscreen) {
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
    c->isfullscreen = 1;
    c->oldstate = c->isfloating;
    c->oldbw = c->bw;
    c->bw = 0;
    c->isfloating = 1;
    resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
    XRaiseWindow(dpy, c->win);
  } else if (!fullscreen && c->isfullscreen){
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)0, 0);
    c->isfullscreen = 0;
    c->isfloating = c->oldstate;
    c->bw = c->oldbw;
    c->x = c->oldx;
    c->y = c->oldy;
    c->w = c->oldw;
    c->h = c->oldh;
    resizeclient(c, c->x, c->y, c->w, c->h);
    arrange(c->mon);
  }
}

void
setgaps(int oh, int ov, int ih, int iv)
{
  if (oh < 0) oh = 0;
  if (ov < 0) ov = 0;
  if (ih < 0) ih = 0;
  if (iv < 0) iv = 0;

  selmon->gappoh = oh;
  selmon->gappov = ov;
  selmon->gappih = ih;
  selmon->gappiv = iv;
  arrange(selmon);
}

void
togglegaps(const Arg *arg)
{
  enablegaps = !enablegaps;
  arrange(selmon);
}

void
defaultgaps(const Arg *arg)
{
  setgaps(gappoh, gappov, gappih, gappiv);
}

void
incrgaps(const Arg *arg)
{
  setgaps(
    selmon->gappoh + arg->i,
    selmon->gappov + arg->i,
    selmon->gappih + arg->i,
    selmon->gappiv + arg->i
  );
}

void
incrigaps(const Arg *arg)
{
  setgaps(
    selmon->gappoh,
    selmon->gappov,
    selmon->gappih + arg->i,
    selmon->gappiv + arg->i
  );
}

void
incrogaps(const Arg *arg)
{
  setgaps(
    selmon->gappoh + arg->i,
    selmon->gappov + arg->i,
    selmon->gappih,
    selmon->gappiv
  );
}

void
incrohgaps(const Arg *arg)
{
  setgaps(
    selmon->gappoh + arg->i,
    selmon->gappov,
    selmon->gappih,
    selmon->gappiv
  );
}

void
incrovgaps(const Arg *arg)
{
  setgaps(
    selmon->gappoh,
    selmon->gappov + arg->i,
    selmon->gappih,
    selmon->gappiv
  );
}

void
incrihgaps(const Arg *arg)
{
  setgaps(
    selmon->gappoh,
    selmon->gappov,
    selmon->gappih + arg->i,
    selmon->gappiv
  );
}

void
incrivgaps(const Arg *arg)
{
  setgaps(
    selmon->gappoh,
    selmon->gappov,
    selmon->gappih,
    selmon->gappiv + arg->i
  );
}

void
setlayout(const Arg *arg)
{
  if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
    selmon->sellt ^= 1;
  if (arg && arg->v)
    selmon->lt[selmon->sellt] = (Layout *)arg->v;
  strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);
  if (selmon->sel)
    arrange(selmon);
  else
    drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
  float f;

  if (!arg || !selmon->lt[selmon->sellt]->arrange)
    return;
  f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
  if (f < 0.05 || f > 0.95)
    return;
  selmon->mfact = f;
  arrange(selmon);
}

void
setup(void)
{
  int i;
  XSetWindowAttributes wa;
  Atom utf8string;
  struct sigaction sa;

  /* do not transform children into zombies when they terminate */
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
  sa.sa_handler = SIG_IGN;
  sigaction(SIGCHLD, &sa, NULL);

  /* clean up any zombies (inherited from .xinitrc etc) immediately */
  while (waitpid(-1, NULL, WNOHANG) > 0);

  /* init screen */
  screen = DefaultScreen(dpy);
  sw = DisplayWidth(dpy, screen);
  sh = DisplayHeight(dpy, screen);
  root = RootWindow(dpy, screen);
  xinitvisual();
  drw = drw_create(dpy, screen, root, sw, sh, visual, depth, cmap);
  if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
    die("no fonts could be loaded.");
  lrpad = barlrpad;
  bh = MAX(barheight, drw->fonts->h + 2);
  updategeom();
  /* init atoms */
  utf8string = XInternAtom(dpy, "UTF8_STRING", False);
  wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
  wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
  wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
  netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
  netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
  netatom[NetSystemTray] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_S0", False);
  netatom[NetSystemTrayOP] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
  netatom[NetSystemTrayOrientation] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
  netatom[NetSystemTrayOrientationHorz] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION_HORZ", False);
  netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
  netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
  netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
  netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
  netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
  xatom[Manager] = XInternAtom(dpy, "MANAGER", False);
  xatom[Xembed] = XInternAtom(dpy, "_XEMBED", False);
  xatom[XembedInfo] = XInternAtom(dpy, "_XEMBED_INFO", False);
  /* init cursors */
  cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
  cursor[CurResize] = drw_cur_create(drw, XC_sizing);
  cursor[CurMove] = drw_cur_create(drw, XC_fleur);
  /* init appearance */
  scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
  for (i = 0; i < LENGTH(colors); i++)
    scheme[i] = drw_scm_create(drw, colors[i], alphas[i], 3);
  /* init system tray */
  updatesystray();
  /* init bars */
  updatebars();
  updatestatus();
  /* supporting window for NetWMCheck */
  wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *) &wmcheckwin, 1);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
                  PropModeReplace, (unsigned char *) "dwm", 3);
  XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *) &wmcheckwin, 1);
  /* EWMH support per view */
  XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
                  PropModeReplace, (unsigned char *) netatom, NetLast);
  XDeleteProperty(dpy, root, netatom[NetClientList]);
  /* select events */
  wa.cursor = cursor[CurNormal]->cursor;
  wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
    |ButtonPressMask|PointerMotionMask|EnterWindowMask
    |LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
  XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
  XSelectInput(dpy, root, wa.event_mask);
  grabkeys();
  focus(NULL);
}

void
seturgent(Client *c, int urg)
{
  XWMHints *wmh;

  c->isurgent = urg;
  if (!(wmh = XGetWMHints(dpy, c->win)))
    return;
  wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
  XSetWMHints(dpy, c->win, wmh);
  XFree(wmh);
}

void
show(const Arg *arg)
{
  if (selmon->hidsel)
    selmon->hidsel = 0;
  showwin(selmon->sel);
}

void
showall(const Arg *arg)
{
  Client *c = NULL;
  selmon->hidsel = 0;
  for (c = selmon->clients; c; c = c->next) {
    if (ISVISIBLE(c))
      showwin(c);
  }
  if (!selmon->sel) {
    for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
    if (c)
      focus(c);
  }
  restack(selmon);
}

void
showwin(Client *c)
{
  if (!c || !HIDDEN(c))
    return;

  XMapWindow(dpy, c->win);
  setclientstate(c, NormalState);
  arrange(c->mon);
}

void
showhide(Client *c)
{
  if (!c)
    return;
  if (ISVISIBLE(c)) {
    /* show clients top down */
    XMoveWindow(dpy, c->win, c->x, c->y);
    if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) && !c->isfullscreen)
      resize(c, c->x, c->y, c->w, c->h, 0);
    showhide(c->snext);
  } else {
    /* hide clients bottom up */
    showhide(c->snext);
    XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
  }
}

void
spawn(const Arg *arg)
{
  struct sigaction sa;

  if (arg->v == dmenucmd)
    dmenumon[0] = '0' + selmon->num;
  if (fork() == 0) {
    if (dpy)
      close(ConnectionNumber(dpy));
    if (arg->v == statuscmd) {
      for (int i = 0; i < LENGTH(statuscmds); i++) {
        if (statuscmdn == statuscmds[i].id) {
          statuscmd[2] = statuscmds[i].cmd;
          setenv("BUTTON", lastbutton, 1);
          break;
        }
      }
      if (!statuscmd[2])
        exit(EXIT_SUCCESS);
    }
    setsid();

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &sa, NULL);

    execvp(((char **)arg->v)[0], (char **)arg->v);
    die("dwm: execvp '%s' failed:", ((char **)arg->v)[0]);
  }
}

void
tag(const Arg *arg)
{
  if (selmon->sel && arg->ui & TAGMASK) {
    selmon->sel->tags = arg->ui & TAGMASK;
    focus(NULL);
    arrange(selmon);
  }
}

void
tagmon(const Arg *arg)
{
  if (!selmon->sel || !mons->next)
    return;
  sendmon(selmon->sel, dirtomon(arg->i));
  focusmon(arg);
}

void
tile(Monitor *m)
{
  /**
   * i, n, h, r：用于循环计数和计算窗口高度。
   * oe, ie：表示是否启用外部和内部间隙。
   * mw：主区域的宽度。
   * my, ty：主区域和堆栈区域的起始位置。
   * c：指向当前处理的窗口。
   */
  unsigned int i, n, h, r, oe = enablegaps, ie = enablegaps, mw, my, ty;
  Client *c;

  // 计算当前屏幕上可平铺的窗口数量
  for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
  if (n == 0)
    return;

  // 如果启用了智能间隙，并且窗口数量等于智能间隙的数量，则禁用外部间隙
  if (smartgaps == n) {
    oe = 0; // outer gaps disabled
  }

  // 计算主区域的宽度
  if (n > m->nmaster)
    mw = m->nmaster ? (m->ww + m->gappiv*ie) * m->mfact : 0;
  else
    mw = m->ww - 2*m->gappov*oe + m->gappiv*ie;

  // 初始化主区域和堆栈区域的起始位置
  for (i = 0, my = ty = m->gappoh*oe, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
    if (i < m->nmaster) {
      // 计算主区域中每个窗口的高度
      r = MIN(n, m->nmaster) - i;
      h = (m->wh - my - m->gappoh*oe - m->gappih*ie * (r - 1)) / r;
      // 调整窗口大小和位置
      resize(c, m->wx + m->gappov*oe, m->wy + my, mw - (2*c->bw) - m->gappiv*ie, h - (2*c->bw), 0);
      // 更新下一个窗口的起始位置
      if (my + HEIGHT(c) + m->gappih*ie < m->wh)
        my += HEIGHT(c) + m->gappih*ie;
    } else {
      // 计算堆栈区域中每个窗口的高度
      r = n - i;
      h = (m->wh - ty - m->gappoh*oe - m->gappih*ie * (r - 1)) / r;
      // 调整窗口大小和位置
      resize(c, m->wx + mw + m->gappov*oe, m->wy + ty, m->ww - mw - (2*c->bw) - 2*m->gappov*oe, h - (2*c->bw), 0);
      // 更新下一个窗口的起始位置
      if (ty + HEIGHT(c) + m->gappih*ie < m->wh)
        ty += HEIGHT(c) + m->gappih*ie;
    }
}

void
magicgrid(Monitor *m)
{
  unsigned int i, n, oe = enablegaps, ie = enablegaps;
  unsigned int cx, cy, cw, ch;
  unsigned int dx;
  unsigned int cols, rows, overcols;
  Client *c;

  for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
  if (n == 0) return;
  if (n == 1) {
    c = nexttiled(m->clients);
    cw = (m->ww - m->gappov*oe) * 0.7;
    ch = (m->wh - m->gappoh*oe) * 0.7;
    resize(c,
           m->mx + (m->mw - cw) / 2 + m->gappov*oe,
           m->my + (m->mh - ch) / 2 + m->gappoh*oe,
           cw - 2 * c->bw,
           ch - 2 * c->bw,
           0);
    return;
  }
  if (n == 2) {
    c = nexttiled(m->clients);
    cw = (m->ww - m->gappov*oe - m->gappiv*ie) / 2;
    ch = (m->wh - m->gappoh*oe - m->gappih*ie) * 0.7;
    resize(c,
           m->mx + m->gappov*oe,
           m->my + (m->mh - ch) / 2 + m->gappoh*oe,
           cw - 2 * c->bw,
           ch - 2 * c->bw,
           0);
    resize(nexttiled(c->next),
           m->mx + cw + m->gappov*oe + m->gappiv*ie,
           m->my + (m->mh - ch) / 2 + m->gappoh*oe,
           cw - 2 * c->bw,
           ch - 2 * c->bw,
           0);
    return;
  }

  for (cols = 0; cols <= n / 2; cols++)
    if (cols * cols >= n)
      break;
  rows = (cols && (cols - 1) * cols >= n) ? cols - 1 : cols;
  ch = (m->wh - 2*m->gappoh*oe - (rows - 1) * m->gappih*ie) / rows;
  cw = (m->ww - 2*m->gappov*oe - (cols - 1) * m->gappiv*ie) / cols;

  overcols = n % cols;
  if (overcols) dx = (m->ww - overcols * cw - (overcols - 1) * m->gappiv*ie) / 2 - m->gappov*oe;
  for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++) {
    cx = m->wx + (i % cols) * (cw + m->gappiv*ie);
    cy = m->wy + (i / cols) * (ch + m->gappih*ie);
    if (overcols && i >= n - overcols) {
      cx += dx;
    }
    resize(c,
           cx + m->gappov*oe,
           cy + m->gappoh*oe,
           cw - 2 * c->bw,
           ch - 2 * c->bw,
           0);
  }
}

void
togglebar(const Arg *arg)
{
  selmon->showbar = !selmon->showbar;
  updatebarpos(selmon);
  XMoveResizeWindow(dpy, selmon->barwin, selmon->wx, selmon->by, selmon->ww, bh);
  resizebarwin(selmon);
  if (showsystray) {
    XWindowChanges wc;
    if (!selmon->showbar)
      wc.y = -bh;
    else if (selmon->showbar) {
      wc.y = 0;
      if (!selmon->topbar)
        wc.y = selmon->mh - bh;
    }
    XConfigureWindow(dpy, systray->win, CWY, &wc);
  }
  arrange(selmon);
}

void
togglefloating(const Arg *arg)
{
  if (!selmon->sel)
    return;
  if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
    return;
  selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
  if (selmon->sel->isfloating)
    resize(selmon->sel, selmon->sel->x, selmon->sel->y,
           selmon->sel->w, selmon->sel->h, 0);
  arrange(selmon);
}

void
toggletag(const Arg *arg)
{
  unsigned int newtags;

  if (!selmon->sel)
    return;
  newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
  if (newtags) {
    selmon->sel->tags = newtags;
    focus(NULL);
    arrange(selmon);
  }
}

void
toggleview(const Arg *arg)
{
  unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

  if (newtagset) {
    selmon->tagset[selmon->seltags] = newtagset;
    focus(NULL);
    arrange(selmon);
  }
}

void
togglewin(const Arg *arg)
{
  Client *c = (Client*)arg->v;

  /* 修复：点击空标签的标题栏区域时 arg->v 为 NULL，直接返回 */
  if (!c)
    return;

  if (c == selmon->sel) {
    hidewin(c);
    focus(NULL);
    arrange(c->mon);
  } else {
    if (HIDDEN(c))
      showwin(c);
    focus(c);
    restack(selmon);
  }
}

void
unfocus(Client *c, int setfocus)
{
  if (!c)
    return;
  grabbuttons(c, 0);
  XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
  if (setfocus) {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
}

void
unmanage(Client *c, int destroyed)
{
  Monitor *m = c->mon;
  XWindowChanges wc;

  detach(c);
  detachstack(c);
  if (!destroyed) {
    wc.border_width = c->oldbw;
    XGrabServer(dpy); /* avoid race conditions */
    XSetErrorHandler(xerrordummy);
    XSelectInput(dpy, c->win, NoEventMask);
    XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    setclientstate(c, WithdrawnState);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
  free(c);
  focus(NULL);
  updateclientlist();
  arrange(m);
}

void
unmapnotify(XEvent *e)
{
  Client *c;
  XUnmapEvent *ev = &e->xunmap;

  if ((c = wintoclient(ev->window))) {
    if (ev->send_event)
      setclientstate(c, WithdrawnState);
    else
      unmanage(c, 0);
  }
  else if ((c = wintosystrayicon(ev->window))) {
    /* KLUDGE! sometimes icons occasionally unmap their windows, but do
     * _not_ destroy them. We map those windows back */
    XMapRaised(dpy, c->win);
    updatesystray();
  }
}

void
updatebars(void)
{
  unsigned int w;
  Monitor *m;
  XSetWindowAttributes wa = {
    .override_redirect = True,
    .background_pixel = 0,
    .border_pixel = 0,
    .colormap = cmap,
    .event_mask = ButtonPressMask|ExposureMask
  };
  XClassHint ch = {"dwm", "dwm"};
  for (m = mons; m; m = m->next) {
    if (m->barwin)
      continue;
    w = m->ww;
    if (showsystray && m == systraytomon(m))
      w -= getsystraywidth();
    m->barwin = XCreateWindow(dpy, root, m->wx, m->by, m->ww, bh, 0, depth,
                              InputOutput, visual,
                              CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &wa);
    XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
    if (showsystray && m == systraytomon(m))
      XMapRaised(dpy, systray->win);
    XMapRaised(dpy, m->barwin);
    XSelectInput(dpy, m->barwin, ButtonPressMask|PointerMotionMask);
    XSetClassHint(dpy, m->barwin, &ch);
  }
}

void
updatebarpos(Monitor *m)
{
  m->wy = m->my;
  m->wh = m->mh;
  if (m->showbar) {
    m->wh -= bh;
    m->by = m->topbar ? m->wy : m->wy + m->wh;
    m->wy = m->topbar ? m->wy + bh : m->wy;
  } else
  m->by = -bh;
}

void
updateclientlist()
{
  Client *c;
  Monitor *m;

  XDeleteProperty(dpy, root, netatom[NetClientList]);
  for (m = mons; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      XChangeProperty(dpy, root, netatom[NetClientList],
                      XA_WINDOW, 32, PropModeAppend,
                      (unsigned char *) &(c->win), 1);
}

int
updategeom(void)
{
  int dirty = 0;

#ifdef XINERAMA
  if (XineramaIsActive(dpy)) {
    int i, j, n, nn;
    Client *c;
    Monitor *m;
    XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
    XineramaScreenInfo *unique = NULL;

    for (n = 0, m = mons; m; m = m->next, n++);
    /* only consider unique geometries as separate screens */
    unique = ecalloc(nn, sizeof(XineramaScreenInfo));
    for (i = 0, j = 0; i < nn; i++)
      if (isuniquegeom(unique, j, &info[i]))
        memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
    XFree(info);
    nn = j;

    /* new monitors if nn > n */
    for (i = n; i < nn; i++) {
      for (m = mons; m && m->next; m = m->next);
      if (m)
        m->next = createmon();
      else
        mons = createmon();
    }
    for (i = 0, m = mons; i < nn && m; m = m->next, i++)
      if (i >= n
        || unique[i].x_org != m->mx || unique[i].y_org != m->my
        || unique[i].width != m->mw || unique[i].height != m->mh)
      {
        dirty = 1;
        m->num = i;
        m->mx = m->wx = unique[i].x_org;
        m->my = m->wy = unique[i].y_org;
        m->mw = m->ww = unique[i].width;
        m->mh = m->wh = unique[i].height;
        updatebarpos(m);
      }
    /* removed monitors if n > nn */
    for (i = nn; i < n; i++) {
      for (m = mons; m && m->next; m = m->next);
      while ((c = m->clients)) {
        dirty = 1;
        m->clients = c->next;
        detachstack(c);
        c->mon = mons;
        attach(c);
        attachstack(c);
      }
      if (m == selmon)
        selmon = mons;
      cleanupmon(m);
    }
    free(unique);
  } else
#endif /* XINERAMA */
  { /* default monitor setup */
    if (!mons)
      mons = createmon();
    if (mons->mw != sw || mons->mh != sh) {
      dirty = 1;
      mons->mw = mons->ww = sw;
      mons->mh = mons->wh = sh;
      updatebarpos(mons);
    }
  }
  if (dirty) {
    selmon = mons;
    selmon = wintomon(root);
  }
  return dirty;
}

void
updatenumlockmask(void)
{
  unsigned int i, j;
  XModifierKeymap *modmap;

  numlockmask = 0;
  modmap = XGetModifierMapping(dpy);
  for (i = 0; i < 8; i++)
    for (j = 0; j < modmap->max_keypermod; j++)
      if (modmap->modifiermap[i * modmap->max_keypermod + j]
        == XKeysymToKeycode(dpy, XK_Num_Lock))
        numlockmask = (1 << i);
  XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
  long msize;
  XSizeHints size;

  if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
    /* size is uninitialized, ensure that size.flags aren't used */
    size.flags = PSize;
  if (size.flags & PBaseSize) {
    c->basew = size.base_width;
    c->baseh = size.base_height;
  } else if (size.flags & PMinSize) {
    c->basew = size.min_width;
    c->baseh = size.min_height;
  } else
  c->basew = c->baseh = 0;
  if (size.flags & PResizeInc) {
    c->incw = size.width_inc;
    c->inch = size.height_inc;
  } else
  c->incw = c->inch = 0;
  if (size.flags & PMaxSize) {
    c->maxw = size.max_width;
    c->maxh = size.max_height;
  } else
  c->maxw = c->maxh = 0;
  if (size.flags & PMinSize) {
    c->minw = size.min_width;
    c->minh = size.min_height;
  } else if (size.flags & PBaseSize) {
    c->minw = size.base_width;
    c->minh = size.base_height;
  } else
  c->minw = c->minh = 0;
  if (size.flags & PAspect) {
    c->mina = (float)size.min_aspect.y / size.min_aspect.x;
    c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
  } else
  c->maxa = c->mina = 0.0;
  c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
  c->hintsvalid = 1;
}

void
updatestatus(void)
{
  if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
    strcpy(stext, "dwm-"VERSION);
  statusw = statuswidth(stext);
  drawbar(selmon);
  /* 性能修复：状态栏每秒更新一次时没必要重排 systray。
   * 图标停靠/销毁/几何变化时（clientmessage/destroynotify/propertynotify 等）
   * 会单独调用 updatesystray。原来这里每秒触发一次全量 X 重排 + XSync，
   * 纯属浪费，还伴随 GC 泄漏（已修复）。 */
}
void
updatesystrayicongeom(Client *i, int w, int h)
{
  if (i) {
    i->h = bh;
    if (w == h)
      i->w = bh;
    else if (h == bh)
      i->w = w;
    else
      i->w = (int) ((float)bh * ((float)w / (float)h));
    applysizehints(i, &(i->x), &(i->y), &(i->w), &(i->h), False);
    /* force icons into the systray dimenons if they don't want to */
    if (i->h > bh) {
      if (i->w == i->h)
        i->w = bh;
      else
        i->w = (int) ((float)bh * ((float)i->w / (float)i->h));
      i->h = bh;
    }
  }
}
void
updatesystrayiconstate(Client *i, XPropertyEvent *ev)
{
  long flags;
  int code = 0;
  if (!showsystray || !i || ev->atom != xatom[XembedInfo] ||
    !(flags = getatomprop(i, xatom[XembedInfo])))
    return;
  if (flags & XEMBED_MAPPED && !i->tags) {
    i->tags = 1;
    code = XEMBED_WINDOW_ACTIVATE;
    XMapRaised(dpy, i->win);
    setclientstate(i, NormalState);
  }
  else if (!(flags & XEMBED_MAPPED) && i->tags) {
    i->tags = 0;
    code = XEMBED_WINDOW_DEACTIVATE;
    XUnmapWindow(dpy, i->win);
    setclientstate(i, WithdrawnState);
  }
  else
  return;
  sendevent(i->win, xatom[Xembed], StructureNotifyMask, CurrentTime, code, 0,
            systray->win, XEMBED_EMBEDDED_VERSION);
}
void
updatesystray(void)
{
  XSetWindowAttributes wa;
  XWindowChanges wc;
  Client *i;
  Monitor *m = systraytomon(NULL);
  int x = m->mx + m->mw;
  int y = m->by;
  unsigned int w = 1;
  if (!showsystray)
    return;
  if (!systray) {
    /* init systray */
    if (!(systray = (Systray *)calloc(1, sizeof(Systray))))
      die("fatal: could not malloc() %u bytes\n", sizeof(Systray));
    systray->win = XCreateSimpleWindow(dpy, root, x, m->by, w, bh, 0, 0, scheme[SchemeSel][ColBg].pixel);
    wa.event_mask        = ButtonPressMask | ExposureMask;
    wa.override_redirect = True;
    wa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
    XSelectInput(dpy, systray->win, SubstructureNotifyMask);
    XChangeProperty(dpy, systray->win, netatom[NetSystemTrayOrientation], XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&netatom[NetSystemTrayOrientationHorz], 1);
    XChangeWindowAttributes(dpy, systray->win, CWEventMask|CWOverrideRedirect|CWBackPixel, &wa);
    XMapRaised(dpy, systray->win);
    XSetSelectionOwner(dpy, netatom[NetSystemTray], systray->win, CurrentTime);
    if (XGetSelectionOwner(dpy, netatom[NetSystemTray]) == systray->win) {
      sendevent(root, xatom[Manager], StructureNotifyMask, CurrentTime, netatom[NetSystemTray], systray->win, 0, 0);
      XSync(dpy, False);
    }
    else {
      fprintf(stderr, "dwm: unable to obtain system tray.\n");
      free(systray);
      systray = NULL;
      return;
    }
  }
  for (w = 0, i = systray->icons; i; i = i->next) {
    w += systrayspacing;
    i->x = w;
    w += i->w;
  }
  w = w ? w + systrayspacing : 1;
  x -= w;
  /* 性能修复：图标集合、总宽度、位置、所在 monitor 都没变时直接返回，
   * 避免无谓的 X 重排 + XSync 往返 */
  {
    unsigned int n = 0;
    for (i = systray->icons; i; i = i->next)
      n++;
    if (systray->w == w && systray->n == n &&
        systray->x == x && systray->y == y && systray->m == m)
      return;
    systray->w = w;
    systray->n = n;
    systray->x = x;
    systray->y = y;
    systray->m = m;
  }
  for (i = systray->icons; i; i = i->next) {
    /* make sure the background color stays the same */
    wa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
    XChangeWindowAttributes(dpy, i->win, CWBackPixel, &wa);
    XMapRaised(dpy, i->win);
    XMoveResizeWindow(dpy, i->win, i->x, 0, i->w, i->h);
    if (i->mon != m)
      i->mon = m;
  }
  XMoveResizeWindow(dpy, systray->win, x, y, w, bh);
  wc.x = x; wc.y = y; wc.width = w; wc.height = bh;
  wc.stack_mode = Above; wc.sibling = m->barwin;
  XConfigureWindow(dpy, systray->win, CWX|CWY|CWWidth|CWHeight|CWSibling|CWStackMode, &wc);
  XMapWindow(dpy, systray->win);
  XMapSubwindows(dpy, systray->win);
  /* redraw background */
  if (!systray->gc)
    systray->gc = XCreateGC(dpy, root, 0, NULL);
  XSetForeground(dpy, systray->gc, scheme[SchemeNorm][ColBg].pixel);
  XFillRectangle(dpy, systray->win, systray->gc, 0, 0, w, bh);
  XSync(dpy, False);
}

void
updatetitle(Client *c)
{
  if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
    gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
  if (c->name[0] == '\0') /* hack to mark broken clients */
    strcpy(c->name, broken);
}

void
updatewindowtype(Client *c)
{
  Atom state = getatomprop(c, netatom[NetWMState]);
  Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

  if (state == netatom[NetWMFullscreen])
    setfullscreen(c, 1);
  if (wtype == netatom[NetWMWindowTypeDialog])
    c->isfloating = 1;
}

void
updatewmhints(Client *c)
{
  XWMHints *wmh;

  if ((wmh = XGetWMHints(dpy, c->win))) {
    if (c == selmon->sel && wmh->flags & XUrgencyHint) {
      wmh->flags &= ~XUrgencyHint;
      XSetWMHints(dpy, c->win, wmh);
    } else
    c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
    if (wmh->flags & InputHint)
      c->neverfocus = !wmh->input;
    else
      c->neverfocus = 0;
    XFree(wmh);
  }
}

void
view(const Arg *arg)
{
  if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
    return;
  selmon->seltags ^= 1; /* toggle sel tagset */
  if (arg->ui & TAGMASK)
    selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
  focus(NULL);
  arrange(selmon);
}

Client *
wintoclient(Window w)
{
  Client *c;
  Monitor *m;

  for (m = mons; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      if (c->win == w)
        return c;
  return NULL;
}

Client *
wintosystrayicon(Window w) {
  Client *i = NULL;

  if (!showsystray || !w)
    return i;
  for (i = systray->icons; i && i->win != w; i = i->next) ;
  return i;
}

Monitor *
wintomon(Window w)
{
  int x, y;
  Client *c;
  Monitor *m;

  if (w == root && getrootptr(&x, &y))
    return recttomon(x, y, 1, 1);
  for (m = mons; m; m = m->next)
    if (w == m->barwin)
      return m;
  if ((c = wintoclient(w)))
    return c->mon;
  return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
  if (ee->error_code == BadWindow
    || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
    || (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
    || (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
    || (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
    || (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
    || (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
    return 0;
  fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
          ee->request_code, ee->error_code);
  return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
  return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
  die("dwm: another window manager is already running");
  return -1;
}

void
xinitvisual()
{
  XVisualInfo *infos;
  XRenderPictFormat *fmt;
  int nitems;
  int i;

  XVisualInfo tpl = {
    .screen = screen,
    .depth = 32,
    .class = TrueColor
  };
  long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;

  infos = XGetVisualInfo(dpy, masks, &tpl, &nitems);
  visual = NULL;
  for(i = 0; i < nitems; i ++) {
    fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
    if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
      visual = infos[i].visual;
      depth = infos[i].depth;
      cmap = XCreateColormap(dpy, root, visual, AllocNone);
      useargb = 1;
      break;
    }
  }

  XFree(infos);

  if (! visual) {
    visual = DefaultVisual(dpy, screen);
    depth = DefaultDepth(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
  }
}

Monitor *
systraytomon(Monitor *m) {
  Monitor *t;
  int i, n;
  if(!systraypinning) {
    if(!m)
      return selmon;
    return m == selmon ? m : NULL;
  }
  for(n = 1, t = mons; t && t->next; n++, t = t->next) ;
  for(i = 1, t = mons; t && t->next && i < systraypinning; i++, t = t->next) ;
  if(systraypinningfailfirst && n < systraypinning)
    return mons;
  return t;
}

void
zoom(const Arg *arg)
{
  Client *c = selmon->sel;

  if (!selmon->lt[selmon->sellt]->arrange || !c || c->isfloating)
    return;
  if (c == nexttiled(selmon->clients) && !(c = nexttiled(c->next)))
    return;
  pop(c);
}

void
movewin(const Arg *arg)
{
  Client *c, *tc;
  int nx, ny;
  int buttom, top, left, right, tar;
  c = selmon->sel;
  if (!c || c->isfullscreen)
    return;
  if (!c->isfloating)
    togglefloating(NULL);
  nx = c->x;
  ny = c->y;
  switch (arg->ui) {
    case UP:
      tar = -99999;
      top = c->y;
      ny -= c->mon->wh / 4;
      for (tc = c->mon->clients; tc; tc = tc->next) {
        // 若浮动tc c的顶边会穿过tc的底边
        if (!ISVISIBLE(tc) || !tc->isfloating || tc == c) continue;
        if (c->x + WIDTH(c) < tc->x || c->x > tc->x + WIDTH(tc)) continue;
        buttom = tc->y + HEIGHT(tc);
        if (top > buttom && ny < buttom) {
          tar = MAX(tar, buttom);
        };
      }
      ny = tar == -99999 ? ny : tar;
      ny = MAX(ny, c->mon->wy);
      break;
    case DOWN:
      tar = 99999;
      buttom = c->y + HEIGHT(c);
      ny += c->mon->wh / 4;
      for (tc = c->mon->clients; tc; tc = tc->next) {
        // 若浮动tc c的底边会穿过tc的顶边
        if (!ISVISIBLE(tc) || !tc->isfloating || tc == c) continue;
        if (c->x + WIDTH(c) < tc->x || c->x > tc->x + WIDTH(tc)) continue;
        top = tc->y;
        if (buttom < top && (ny + HEIGHT(c)) > top) {
          tar = MIN(tar, top - HEIGHT(c));
        };
      }
      ny = tar == 99999 ? ny : tar;
      ny = MIN(ny, c->mon->wy + c->mon->wh - HEIGHT(c));
      break;
    case LEFT:
      tar = -99999;
      left = c->x;
      nx -= c->mon->ww / 6;
      for (tc = c->mon->clients; tc; tc = tc->next) {
        // 若浮动tc c的左边会穿过tc的右边
        if (!ISVISIBLE(tc) || !tc->isfloating || tc == c) continue;
        if (c->y + HEIGHT(c) < tc->y || c->y > tc->y + HEIGHT(tc)) continue;
        right = tc->x + WIDTH(tc);
        if (left > right && nx < right) {
          tar = MAX(tar, right);
        };
      }
      nx = tar == -99999 ? nx : tar;
      nx = MAX(nx, c->mon->wx);
      break;
    case RIGHT:
      tar = 99999;
      right = c->x + WIDTH(c);
      nx += c->mon->ww / 6;
      for (tc = c->mon->clients; tc; tc = tc->next) {
        // 若浮动tc c的右边会穿过tc的左边
        if (!ISVISIBLE(tc) || !tc->isfloating || tc == c) continue;
        if (c->y + HEIGHT(c) < tc->y || c->y > tc->y + HEIGHT(tc)) continue;
        left = tc->x;
        if (right < left && (nx + WIDTH(c)) > left) {
          tar = MIN(tar, left - WIDTH(c));
        };
      }
      nx = tar == 99999 ? nx : tar;
      nx = MIN(nx, c->mon->wx + c->mon->ww - WIDTH(c));
      break;
  }
  resize(c, nx, ny, c->w, c->h, 1);
  // pointerclient(c);
  restack(selmon);
}

void
resizewin(const Arg *arg)
{
  Client *c, *tc;
  int nh, nw;
  int buttom, top, left, right, tar;
  c = selmon->sel;
  if (!c || c->isfullscreen)
    return;
  if (!c->isfloating)
    togglefloating(NULL);
  nw = c->w;
  nh = c->h;
  switch (arg->ui) {
    case H_EXPAND: // 右
      tar = 99999;
      right = c->x + WIDTH(c);
      nw += selmon->ww / 16;
      for (tc = c->mon->clients; tc; tc = tc->next) {
        // 若浮动tc c的右边会穿过tc的左边
        if (!ISVISIBLE(tc) || !tc->isfloating || tc == c) continue;
        if (c->y + HEIGHT(c) < tc->y || c->y > tc->y + HEIGHT(tc)) continue;
        left = tc->x;
        if (right < left && (c->x + nw) > left) {
          tar = MIN(tar, left - c->x - 2 * c->bw);
        };
      }
      nw = tar == 99999 ? nw : tar;
      if (c->x + nw + 2 * c->bw > selmon->wx + selmon->ww)
        nw = selmon->wx + selmon->ww - c->x - 2 * c->bw;
      break;
    case H_REDUCE: // 左
      nw -= selmon->ww / 16;
      nw = MAX(nw, selmon->ww / 10);
      break;
    case V_EXPAND: // 下
      tar = -99999;
      buttom = c->y + HEIGHT(c);
      nh += selmon->wh / 8;
      for (tc = c->mon->clients; tc; tc = tc->next) {
        // 若浮动tc c的底边会穿过tc的顶边
        if (!ISVISIBLE(tc) || !tc->isfloating || tc == c) continue;
        if (c->x + WIDTH(c) < tc->x || c->x > tc->x + WIDTH(tc)) continue;
        top = tc->y;
        if (buttom < top && (c->y + nh) > top) {
          tar = MAX(tar, top - c->y - 2 * c->bw);
        };
      }
      nh = tar == -99999 ? nh : tar;
      if (c->y + nh + 2 * c->bw > selmon->wy + selmon->wh)
        nh = selmon->wy + selmon->wh - c->y - 2 * c->bw;
      break;
    case V_REDUCE: // 上
      nh -= selmon->wh / 8;
      nh = MAX(nh, selmon->wh / 10);
      break;
  }
  resize(c, c->x, c->y, nw, nh, 1);
  XWarpPointer(dpy, None, root, 0, 0, 0, 0, c->x + c->w - 2 * c->bw, c->y + c->h - 2 * c->bw);
  restack(selmon);
}

int
main(int argc, char *argv[])
{
  /**
   * 如果命令行参数的数量是2，并且第二个参数是字符串"-v"，则显示版本信息。
   *  argc 是命令行参数的数量。
   *  argv 是一个字符串数组，包含了命令行参数。
   *  strcmp 是一个字符串比较函数，如果两个字符串相等，则返回0。
   */
  if (argc == 2 && !strcmp("-v", argv[1]))
    die("dwm-"VERSION);
  else if (argc != 1)
    die("usage: dwm [-v]");
  if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
    fputs("warning: no locale support\n", stderr);
  if (!(dpy = XOpenDisplay(NULL)))
    die("dwm: cannot open display");
  checkotherwm();
  setup();
#ifdef __OpenBSD__
  if (pledge("stdio rpath proc exec", NULL) == -1)
    die("pledge");
#endif /* __OpenBSD__ */
  scan();
  runAutostart();
  run();
  cleanup();
  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
