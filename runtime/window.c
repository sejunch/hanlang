/* 한랭 런타임 (C): 창 그리기 (X11 + Xft). 창을 쓰는 프로그램에만 붙는다 (-lX11 -lXft).
 * Makefile 이 gcc -S 로 구워 runtime/runtime-window.s 로 (지역 라벨 .L -> .Lwn). */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Display *dpy;
static Window win;
static Pixmap pm;
static GC gc;
static XftDraw *xd;
static XftFont *font;
static XftColor xcol;
static int W, H, scr;
static unsigned long pix;

static void werr(long line, const char *msg) {
    fflush(stdout);
    printf("[실행 오류] %ld행: %s\n", line, msg);
    exit(1);
}
static void need(long line) {
    if (!dpy) werr(line, "창을 먼저 열어야 함 (예: 창 640과 480을 열다.)");
}

static const struct { const char *n; unsigned c; } NAMES[] = {
    {"검정", 0x000000}, {"하양", 0xFFFFFF}, {"흰색", 0xFFFFFF}, {"검은색", 0x000000},
    {"빨강", 0xE53935}, {"주황", 0xFB8C00}, {"노랑", 0xFDD835}, {"초록", 0x43A047},
    {"파랑", 0x1E88E5}, {"남색", 0x3949AB}, {"보라", 0x8E24AA}, {"분홍", 0xF06292},
    {"갈색", 0x6D4C41}, {"회색", 0x9E9E9E}, {"연회색", 0xE0E0E0}, {"하늘", 0x4FC3F7},
};

static void setrgb(unsigned c) {
    XColor x;
    x.red = ((c >> 16) & 255) * 257; x.green = ((c >> 8) & 255) * 257; x.blue = (c & 255) * 257;
    x.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(dpy, DefaultColormap(dpy, scr), &x);
    pix = x.pixel;
    XSetForeground(dpy, gc, pix);
    XRenderColor rc = { x.red, x.green, x.blue, 0xFFFF };
    XftColorFree(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), &xcol);
    XftColorAllocValue(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), &rc, &xcol);
}

void __han_wopen(long w, long h, long line) {
    if (w < 1 || h < 1 || w > 8000 || h > 8000) werr(line, "창 크기는 1~8000 이어야 함");
    if (dpy) werr(line, "창은 하나만 열 수 있음 (보여주다 로 닫은 뒤 다시 열기)");
    dpy = XOpenDisplay(NULL);
    if (!dpy) werr(line, "창을 띄울 화면이 없음 (DISPLAY 가 없음)");
    scr = DefaultScreen(dpy);
    W = (int)w; H = (int)h;
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, W, H, 0, 0, 0xFFFFFF);
    XStoreName(dpy, win, "한랭");
    Xutf8SetWMProperties(dpy, win, "한랭", "한랭", NULL, 0, NULL, NULL, NULL);
    pm = XCreatePixmap(dpy, win, W, H, DefaultDepth(dpy, scr));
    gc = XCreateGC(dpy, pm, 0, NULL);
    xd = XftDrawCreate(dpy, pm, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr));
    font = XftFontOpenName(dpy, scr, "sans-serif:pixelsize=15:lang=ko");
    memset(&xcol, 0, sizeof xcol);
    XRenderColor black = {0, 0, 0, 0xFFFF};
    XftColorAllocValue(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), &black, &xcol);
    setrgb(0xFFFFFF);                                /* 하얀 도화지 */
    XFillRectangle(dpy, pm, gc, 0, 0, W, H);
    setrgb(0x000000);
}

void __han_wcolor(const char *name, long line) {
    need(line);
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (strcmp(name, NAMES[i].n) == 0) { setrgb(NAMES[i].c); return; }
    if (name[0] == '#' && strlen(name) == 7) {
        char *end; unsigned long v = strtoul(name + 1, &end, 16);
        if (*end == 0) { setrgb((unsigned)v); return; }
    }
    char m[300];
    snprintf(m, sizeof m, "'%s' 는 모르는 색 (검정 하양 빨강 주황 노랑 초록 파랑 남색 보라 분홍 갈색 회색 연회색 하늘, 또는 \"#ff8800\")", name);
    werr(line, m);
}

void __han_wrect(long x, long y, long w, long h, long line) {
    need(line);
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    XFillRectangle(dpy, pm, gc, (int)x, (int)y, (unsigned)w, (unsigned)h);
}
void __han_wline(long x1, long y1, long x2, long y2, long line) {
    need(line);
    XDrawLine(dpy, pm, gc, (int)x1, (int)y1, (int)x2, (int)y2);
}
void __han_wpoint(long x, long y, long line) {
    need(line);
    XDrawPoint(dpy, pm, gc, (int)x, (int)y);
}
/* y 는 글자의 위쪽 (기준선이 아니라) — 사각형과 같은 셈법이 헷갈리지 않는다 */
void __han_wtext(const char *s, long x, long y, long line) {
    need(line);
    if (font) XftDrawStringUtf8(xd, &xcol, font, (int)x, (int)y + font->ascent, (const FcChar8 *)s, (int)strlen(s));
}

static void shot(const char *path) {                 /* 픽스맵을 PPM 으로 */
    XImage *im = XGetImage(dpy, pm, 0, 0, W, H, AllPlanes, ZPixmap);
    FILE *f = fopen(path, "wb");
    if (!f || !im) { printf("[실행 오류] 그림을 '%s' 에 저장할 수 없음\n", path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            unsigned long p = XGetPixel(im, x, y);
            unsigned char rgb[3] = { (p >> 16) & 255, (p >> 8) & 255, p & 255 };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    XDestroyImage(im);
}

static void closeAll(void) {
    XftDrawDestroy(xd);
    if (font) XftFontClose(dpy, font);
    XFreePixmap(dpy, pm);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    dpy = NULL;
}

/* 보여주다: 창을 띄우고 닫을 때까지(창의 X, Esc, q) 기다린다 */
void __han_wshow(long line) {
    need(line);
    fflush(stdout);
    const char *sp = getenv("HAN_SCREENSHOT");
    if (sp && *sp) { XSync(dpy, False); shot(sp); closeAll(); return; }
    Atom del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &del, 1);
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(dpy, win);
    for (;;) {
        XEvent e;
        XNextEvent(dpy, &e);
        if (e.type == Expose) XCopyArea(dpy, pm, win, gc, 0, 0, W, H, 0, 0);
        else if (e.type == ClientMessage && (Atom)e.xclient.data.l[0] == del) break;
        else if (e.type == KeyPress) {
            KeySym k = XLookupKeysym(&e.xkey, 0);
            if (k == XK_Escape || k == XK_q) break;
        }
    }
    closeAll();
}
