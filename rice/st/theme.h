/*
 * myDwm appearance block for vanilla st 0.9.x.
 * Replace the declarations with the same names in st's config.h; do not
 * include this file alongside duplicate declarations. See README.md.
 */

static char *font = "JetBrainsMono Nerd Font Mono:pixelsize=16:antialias=true:autohint=true";
static int borderpx = 12;
static float cwscale = 1.0;
static float chscale = 1.05;

static const char *colorname[] = {
    /* normal */
    [0] = "#45475a",
    [1] = "#f38ba8",
    [2] = "#a6e3a1",
    [3] = "#f9e2af",
    [4] = "#89b4fa",
    [5] = "#f5c2e7",
    [6] = "#94e2d5",
    [7] = "#bac2de",

    /* bright */
    [8]  = "#585b70",
    [9]  = "#f38ba8",
    [10] = "#a6e3a1",
    [11] = "#f9e2af",
    [12] = "#89b4fa",
    [13] = "#f5c2e7",
    [14] = "#94e2d5",
    [15] = "#cdd6f4",

    /* st special colors */
    [256] = "#11111b", /* background */
    [257] = "#cdd6f4", /* foreground */
    [258] = "#cba6f7", /* cursor */
    [259] = "#11111b", /* reverse cursor */
};

unsigned int defaultfg = 257;
unsigned int defaultbg = 256;
unsigned int defaultcs = 258;
static unsigned int defaultrcs = 259;
unsigned int defaultitalic = 5;
unsigned int defaultunderline = 4;
static unsigned int cursorshape = 6; /* slim bar cursor */
