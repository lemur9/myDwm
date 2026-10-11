/* See LICENSE file for copyright and license details. */

typedef struct {
	Cursor cursor;
} Cur;

typedef struct Fnt {
	Display *dpy;
	unsigned int h;
	XftFont *xfont;
	FcPattern *pattern;
	struct Fnt *next;
} Fnt;

enum { ColFg, ColBg, ColBorder }; /* Clr scheme index */
typedef XftColor Clr;

typedef struct {
	unsigned int w, h;
	Display *dpy;
	int screen;
	Window root;
    Visual *visual;
    unsigned int depth;
    Colormap cmap;
	Drawable drawable;
	GC gc;
	Clr *scheme;
	Fnt *fonts;
	int premult;
} Drw;

/* Drawable abstraction */
Drw *drw_create(Display *dpy, int screen, Window win, unsigned int w, unsigned int h, Visual *visual, unsigned int depth, Colormap cmap);
void drw_resize(Drw *drw, unsigned int w, unsigned int h);
void drw_free(Drw *drw);

/* Fnt abstraction */
Fnt *drw_fontset_create(Drw* drw, const char *fonts[], size_t fontcount);
void drw_fontset_free(Fnt* set);
unsigned int drw_fontset_getwidth(Drw *drw, const char *text);
unsigned int drw_fontset_getwidth_clamp(Drw *drw, const char *text, unsigned int n);
void drw_font_getexts(Fnt *font, const char *text, unsigned int len, unsigned int *w, unsigned int *h);

/* Colorscheme abstraction */
void drw_clr_create(Drw *drw, Clr *dest, const char *clrname, unsigned int alpha);
Clr *drw_scm_create(Drw *drw, const char *clrnames[], const unsigned int alphas[], size_t clrcount);

/* Cursor abstraction */
Cur *drw_cur_create(Drw *drw, int shape);
void drw_cur_free(Drw *drw, Cur *cursor);

/* Drawing context manipulation */
void drw_setfontset(Drw *drw, Fnt *set);
void drw_setscheme(Drw *drw, Clr *scm);
/* 半透明颜色是否按 alpha 预乘。ARGB 窗口内容在合成器里按预乘解释，
 * 不预乘的话低 alpha 的颜色会明显偏浓。 */
void drw_set_premult(Drw *drw, int on);

/* Drawing functions */
void drw_rect(Drw *drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert);
void drw_ellipse(Drw *drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert);
int drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, const char *text, int invert);
/* 只画字，不填背景：胶囊底色是圆角的，矩形填充会把圆角盖掉 */
int drw_text_nofill(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, const char *text, int invert);
/* 直接用像素值（含 alpha 字节）填充，绕过当前 scheme */
void drw_fill(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned long pixel);
void drw_fill_rounded(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int r, unsigned long pixel);
void drw_draw_rounded(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int r, unsigned long pixel);

/* Map functions */
void drw_map(Drw *drw, Window win, int x, int y, unsigned int w, unsigned int h);
