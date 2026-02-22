#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xproto.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <X11/XF86keysym.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

typedef union {
  int i;
  unsigned int ui;
  float f;
  const void *v;
} Arg;

typedef struct {
  unsigned int mod;
  KeySym keysym;
  void (*func)(const Arg *arg);
  const Arg arg;
} Key;

typedef struct {
  const char *class;
  int isfloating;
} Rule;

typedef struct Client Client;
struct Client {
  Window win;
  int x, y, w, h;
  unsigned int tags;
  int ignoreunmap;
  int isfloating;
  Client *next;
  Client *prev;
};

static void arrange(void);
static void attach(Client *c);
static void applyrules(Client *c);
static void cleanup(void);
static void configure(Client *c);
static void configurerequest(XEvent *e);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void drawbar(void);
static void focus(Client *c);
static void focusstack(const Arg *arg);
static void grabkeys(void);
static void incmfact(const Arg *arg);
static void incgaps(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void maprequest(XEvent *e);
static void monocle(void);
static void movefocus(Client *c);
static void movestack(const Arg *arg);
static void promotemaster(const Arg *arg);
static void quit(const Arg *arg);
static void resize(Client *c, int x, int y, int w, int h);
static void run(void);
static void setlayout(const Arg *arg);
static void setup(void);
static void spawn(const Arg *arg);
static void tile(void);
static void togglebar(const Arg *arg);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatenumlockmask(void);
static void updateclock(void);
static int getusedram(char *buf, size_t buflen);
static int getvolume(char *buf, size_t buflen);
static int textwidth(const char *text);
static long long nowms(void);
static void view(const Arg *arg);
static void tagandview(const Arg *arg);
static Client *nexttiled(Client *c);
static Client *prevtiled(Client *c);
static Client *wintoclient(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);

#define TAGMASK ((1u << 10) - 1)
#define LENGTH(X) (sizeof(X) / sizeof((X)[0]))
#define TAGKEYS(KEYSYM, TAG) \
  { MODKEY, KEYSYM, view, { .ui = 1u << (TAG) } }, \
  { MODKEY|ShiftMask, KEYSYM, tagandview, { .ui = 1u << (TAG) } }

static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" };

enum { LAYOUT_TILE, LAYOUT_MONOCLE };

#if __has_include("config.h")
#include "config.h"
#else
#include "config.def.h"
#endif

static Display *dpy;
static int screen;
static Window root;
static Window barwin;
static GC gc;
static XftFont *xftfont;
static XftDraw *xftdraw;
static XftColor xftcol_fg;
static XftColor xftcol_bg;
static XftColor xftcol_accent;
static unsigned long col_bg;
static unsigned long col_fg;
static unsigned long col_accent;
static unsigned long col_border_focus;
static unsigned long col_border_norm;
static Atom wmatom;
static Atom net_wm_window_type;
static Atom net_wm_window_type_dock;
static Atom net_wm_state;
static Atom net_wm_state_above;
static int tagx[10];
static int tagw[10];
static int sw, sh;
static int running = 1;
static int starting = 1;
static int statusdirty = 0;
static int status_refresh_pending = 0;
static long long status_refresh_at_ms = 0;
static unsigned int numlockmask = 0;
static Client *clients = NULL;
static Client *sel = NULL;
static unsigned int tagset = 1;
static int layout = LAYOUT_TILE;
static int showbar = 1;
static char centertext[64] = {0};
static char righttext[96] = {0};
static int has_icon_vol = 0;
static int has_icon_mute = 0;
static int has_icon_mem = 0;

static void die(const char *msg) {
  fprintf(stderr, "opendwm: %s\n", msg);
  exit(1);
}

static void attach(Client *c) {
  c->next = clients;
  c->prev = NULL;
  if (clients)
    clients->prev = c;
  clients = c;
}

static void detach(Client *c) {
  if (c->prev)
    c->prev->next = c->next;
  else
    clients = c->next;
  if (c->next)
    c->next->prev = c->prev;
  c->next = c->prev = NULL;
}

static Client *wintoclient(Window w) {
  for (Client *c = clients; c; c = c->next)
    if (c->win == w)
      return c;
  return NULL;
}

static void applyrules(Client *c) {
  XClassHint ch = {0};
  c->isfloating = 0;
  if (!XGetClassHint(dpy, c->win, &ch))
    return;
  for (unsigned int i = 0; rules[i].class; i++) {
    if (ch.res_class && strcmp(rules[i].class, ch.res_class) == 0) {
      c->isfloating = rules[i].isfloating;
      break;
    }
  }
  if (ch.res_class)
    XFree(ch.res_class);
  if (ch.res_name)
    XFree(ch.res_name);
}

static int isvisible(Client *c) {
  return c->tags & tagset;
}

static Client *nexttiled(Client *c) {
  for (; c && (!isvisible(c) || c->isfloating); c = c->next) {
  }
  return c;
}

static Client *prevtiled(Client *c) {
  for (; c && (!isvisible(c) || c->isfloating); c = c->prev) {
  }
  return c;
}

static void movefocus(Client *c) {
  if (!c)
    return;
  focus(c);
}

static void focus(Client *c) {
  if (!c || !isvisible(c))
    return;
  XWindowAttributes wa;
  if (!XGetWindowAttributes(dpy, c->win, &wa) || wa.map_state != IsViewable)
    return;
  sel = c;
  XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
  XRaiseWindow(dpy, c->win);
  for (Client *it = clients; it; it = it->next) {
    if (!isvisible(it))
      continue;
    if (layout == LAYOUT_MONOCLE) {
      XSetWindowBorderWidth(dpy, it->win, 0);
    } else {
      XSetWindowBorderWidth(dpy, it->win, borderpx);
      XSetWindowBorder(dpy, it->win, (it == sel) ? col_border_focus : col_border_norm);
    }
  }
  drawbar();
  if (topbar && showbar)
    XRaiseWindow(dpy, barwin);
}

static void configure(Client *c) {
  XConfigureEvent ce;
  ce.type = ConfigureNotify;
  ce.display = dpy;
  ce.event = c->win;
  ce.window = c->win;
  ce.x = c->x;
  ce.y = c->y;
  ce.width = c->w;
  ce.height = c->h;
  ce.border_width = (layout == LAYOUT_MONOCLE) ? 0 : borderpx;
  ce.above = None;
  ce.override_redirect = False;
  XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

static void resize(Client *c, int x, int y, int w, int h) {
  XWindowAttributes wa;
  if (!XGetWindowAttributes(dpy, c->win, &wa))
    return;
  if (wa.map_state == IsUnmapped)
    return;
  c->x = x;
  c->y = y;
  c->w = w;
  c->h = h;
  XMoveResizeWindow(dpy, c->win, x, y, w, h);
  configure(c);
}

static void manage(Window w, XWindowAttributes *wa) {
  Client *c = calloc(1, sizeof(Client));
  if (!c)
    die("calloc failed");
  c->win = w;
  c->x = wa->x;
  c->y = wa->y;
  c->w = wa->width;
  c->h = wa->height;
  c->tags = tagset;
  applyrules(c);
  if (c->isfloating) {
    int by = (showbar ? barheight : 0);
    int x = (sw - c->w) / 2;
    int y = (sh - by - c->h) / 2 + by;
    if (x < 0)
      x = 0;
    if (y < by)
      y = by;
    c->x = x;
    c->y = y;
    XMoveResizeWindow(dpy, w, c->x, c->y, c->w, c->h);
  }
  attach(c);
  XSelectInput(dpy, w, ButtonPressMask | EnterWindowMask | FocusChangeMask | PropertyChangeMask | StructureNotifyMask);
  XSetWindowBorderWidth(dpy, w, (layout == LAYOUT_MONOCLE) ? 0 : borderpx);
  XSetWindowBorder(dpy, w, col_border_norm);
  XMapWindow(dpy, w);
  focus(c);
  arrange();
}

static void unmanage(Client *c, int destroyed) {
  if (!c)
    return;
  if (!destroyed) {
    XGrabServer(dpy);
    XSetWindowBorderWidth(dpy, c->win, 0);
    XUngrabServer(dpy);
  }
  detach(c);
  if (sel == c)
    sel = NULL;
  free(c);
  focus(nexttiled(clients));
  arrange();
}

static void focusstack(const Arg *arg) {
  Client *c = NULL;
  if (!sel)
    return;
  if (arg->i > 0) {
    if (!(c = nexttiled(sel->next)))
      c = nexttiled(clients);
  } else {
    if (!(c = prevtiled(sel->prev))) {
      for (c = clients; c && c->next; c = c->next) {
      }
      c = prevtiled(c);
    }
  }
  movefocus(c);
}

static void movestack(const Arg *arg) {
  Client *c = NULL;
  if (!sel)
    return;
  if (arg->i > 0) {
    c = nexttiled(sel->next);
    if (!c)
      return;
    detach(sel);
    sel->next = c->next;
    sel->prev = c;
    if (c->next)
      c->next->prev = sel;
    c->next = sel;
  } else {
    c = prevtiled(sel->prev);
    if (!c)
      return;
    detach(sel);
    sel->prev = c->prev;
    sel->next = c;
    if (c->prev)
      c->prev->next = sel;
    else
      clients = sel;
    c->prev = sel;
  }
  arrange();
}

static void setlayout(const Arg *arg) {
  if (arg->i == layout)
    return;
  layout = arg->i;
  arrange();
}

static void incmfact(const Arg *arg) {
  if (!arg)
    return;
  mfact += arg->f;
  int ww = sw - 2 * gappx;
  float minfact = (ww > 0) ? ((float)minmasterw / (float)ww) : 0.05f;
  float maxfact = 0.95f;
  if (minfact < 0.05f)
    minfact = 0.05f;
  if (ww > 0 && minstackw > 0) {
    float stackmax = 1.0f - ((float)minstackw / (float)ww);
    if (stackmax < maxfact)
      maxfact = stackmax;
  }
  if (mfact < minfact)
    mfact = minfact;
  if (mfact > maxfact)
    mfact = maxfact;
  arrange();
}

static void incgaps(const Arg *arg) {
  int next = gappx + arg->i;
  if (next < 0)
    next = 0;
  if (next > maxgaps)
    next = maxgaps;
  gappx = next;
  arrange();
}

static void view(const Arg *arg) {
  if ((arg->ui & TAGMASK) == 0)
    return;
  tagset = arg->ui & TAGMASK;
  arrange();
}

static void tagandview(const Arg *arg) {
  if ((arg->ui & TAGMASK) == 0)
    return;
  if (sel)
    sel->tags = arg->ui & TAGMASK;
  tagset = arg->ui & TAGMASK;
  arrange();
}

static void tile(void) {
  unsigned int n = 0;
  for (Client *c = clients; c; c = c->next)
    if (isvisible(c) && !c->isfloating)
      n++;
  if (n == 0)
    return;

  int wx = 0;
  int wy = showbar ? barheight : 0;
  int ww = sw;
  int wh = sh - (showbar ? barheight : 0);

  int g = gappx;
  wx += g;
  wy += g;
  ww -= 2 * g;
  wh -= 2 * g;

  unsigned int mcount = (n < nmaster) ? n : nmaster;
  unsigned int scount = (n > nmaster) ? (n - nmaster) : 0;

  int mw = (scount > 0) ? (int)(ww * mfact) : ww;
  if (mw < minmasterw)
    mw = minmasterw;
  int swidth = ww - mw;
  if (scount > 0 && swidth < minstackw) {
    mw = ww - minstackw;
    if (mw < minmasterw)
      mw = minmasterw;
    swidth = ww - mw;
  }
  if (scount > 0)
    swidth -= g;

  int mx = wx;
  int sx = wx + mw + g;
  int my = wy;
  int sy = wy;

  unsigned int i = 0;
  for (Client *c = nexttiled(clients); c; c = nexttiled(c->next), i++) {
    if (i < mcount) {
      int mh = (wh - (int)(g * (mcount - 1))) / (int)mcount;
      int mrem = (wh - (int)(g * (mcount - 1))) % (int)mcount;
      int ch = mh + ((int)i == (int)mcount - 1 ? mrem : 0);
      resize(c, mx, my, mw, ch);
      my += ch + g;
    } else {
      unsigned int si = i - mcount;
      int shh = (wh - (int)(g * (scount - 1))) / (int)scount;
      int srem = (wh - (int)(g * (scount - 1))) % (int)scount;
      int ch = shh + ((int)si == (int)scount - 1 ? srem : 0);
      resize(c, sx, sy, swidth, ch);
      sy += ch + g;
    }
  }
}

static void monocle(void) {
  int wx = 0;
  int wy = showbar ? barheight : 0;
  int ww = sw;
  int wh = sh - (showbar ? barheight : 0);
  for (Client *c = clients; c; c = c->next) {
    if (!isvisible(c) || c->isfloating)
      continue;
    XSetWindowBorderWidth(dpy, c->win, 0);
    resize(c, wx, wy, ww, wh);
  }
}

static void arrange(void) {
  for (Client *c = clients; c; c = c->next) {
    if (isvisible(c))
      XMapWindow(dpy, c->win);
    else {
      XUnmapWindow(dpy, c->win);
      c->ignoreunmap++;
    }
  }
  if (sel && !isvisible(sel))
    sel = nexttiled(clients);
  if (layout == LAYOUT_MONOCLE)
    monocle();
  else
    tile();
  if (topbar && showbar)
    XRaiseWindow(dpy, barwin);
  if (sel)
    focus(sel);
  else {
    Client *c = nexttiled(clients);
    if (c)
      focus(c);
    else
      XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
  }
  drawbar();
}

static void drawbar(void) {
  if (!topbar || !showbar)
    return;
  int x = 0;
  XSetForeground(dpy, gc, col_bg);
  XFillRectangle(dpy, barwin, gc, 0, 0, sw, barheight);

  for (unsigned int i = 0; i < LENGTH(tags); i++) {
    int tw = textwidth(tags[i]);
    int pad = 8;
    int w = tw + pad * 2;
    tagx[i] = x;
    tagw[i] = w;
    int occupied = 0;
    for (Client *c = clients; c; c = c->next) {
      if (c->tags & (1u << i)) {
        occupied = 1;
        break;
      }
    }
    unsigned int mask = 1u << i;
    if (tagset & mask) {
      XSetForeground(dpy, gc, col_accent);
      XFillRectangle(dpy, barwin, gc, x, 0, w, barheight);
      XftDrawStringUtf8(xftdraw, &xftcol_bg, xftfont, x + pad,
                        (barheight + xftfont->ascent - xftfont->descent) / 2,
                        (const FcChar8 *)tags[i], (int)strlen(tags[i]));
    } else {
      XftDrawStringUtf8(xftdraw, &xftcol_fg, xftfont, x + pad,
                        (barheight + xftfont->ascent - xftfont->descent) / 2,
                        (const FcChar8 *)tags[i], (int)strlen(tags[i]));
    }
    if (occupied) {
      int sz = 4;
      int rx = x + w - sz - 3;
      int ry = 3;
      XSetForeground(dpy, gc, col_accent);
      XFillRectangle(dpy, barwin, gc, rx, ry, sz, sz);
    }
    x += w;
  }

  const char *layouttxt = (layout == LAYOUT_MONOCLE) ? "[M]" : "[T]";
  int lw = textwidth(layouttxt);
  int lpad = 10;
  XftDrawStringUtf8(xftdraw, &xftcol_fg, xftfont, x + lpad,
                    (barheight + xftfont->ascent - xftfont->descent) / 2,
                    (const FcChar8 *)layouttxt, (int)strlen(layouttxt));
  x += lw + lpad * 2;

  int rightwidth = textwidth(righttext);
  int rightx = sw - rightwidth - 10;
  if (rightx < x)
    rightx = x + 10;
  if (rightwidth > 0) {
    XftDrawStringUtf8(xftdraw, &xftcol_fg, xftfont, rightx,
                      (barheight + xftfont->ascent - xftfont->descent) / 2,
                      (const FcChar8 *)righttext, (int)strlen(righttext));
  }

  int centerwidth = textwidth(centertext);
  if (centerwidth > 0) {
    int leftbound = x + 10;
    int rightbound = (rightwidth > 0) ? (rightx - 10) : (sw - 10);
    int centerx = (sw - centerwidth) / 2;
    if (centerx < leftbound)
      centerx = leftbound;
    if (centerx + centerwidth > rightbound)
      centerx = rightbound - centerwidth;
    if (centerx < leftbound)
      centerx = leftbound;
    if (rightbound > leftbound) {
      XftDrawStringUtf8(xftdraw, &xftcol_fg, xftfont, centerx,
                        (barheight + xftfont->ascent - xftfont->descent) / 2,
                        (const FcChar8 *)centertext, (int)strlen(centertext));
    }
  }

  XFlush(dpy);
}

static int getusedram(char *buf, size_t buflen) {
  FILE *fp = fopen("/proc/meminfo", "r");
  if (!fp)
    return 0;
  long total_kb = -1;
  long avail_kb = -1;
  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "MemTotal:", 9) == 0) {
      sscanf(line + 9, "%ld", &total_kb);
    } else if (strncmp(line, "MemAvailable:", 13) == 0) {
      sscanf(line + 13, "%ld", &avail_kb);
    }
    if (total_kb >= 0 && avail_kb >= 0)
      break;
  }
  fclose(fp);
  if (total_kb <= 0 || avail_kb < 0)
    return 0;
  long used_kb = total_kb - avail_kb;
  double used_gb = (double)used_kb / (1024.0 * 1024.0);
  snprintf(buf, buflen, "%.2f GB", used_gb);
  return 1;
}

static int utf8_first_codepoint(const char *text, FcChar32 *out) {
  const unsigned char *p = (const unsigned char *)text;
  if (!p || !*p)
    return 0;
  if (*p < 0x80) {
    *out = *p;
    return 1;
  }
  if ((*p & 0xE0) == 0xC0) {
    if ((p[1] & 0xC0) != 0x80)
      return 0;
    *out = (FcChar32)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F));
    return 1;
  }
  if ((*p & 0xF0) == 0xE0) {
    if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80)
      return 0;
    *out = (FcChar32)(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
    return 1;
  }
  if ((*p & 0xF8) == 0xF0) {
    if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80)
      return 0;
    *out = (FcChar32)(((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12)
        | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F));
    return 1;
  }
  return 0;
}

static int font_has_glyph(const char *icon) {
  FcChar32 cp = 0;
  if (!xftfont)
    return 0;
  if (!utf8_first_codepoint(icon, &cp))
    return 0;
  return XftCharExists(dpy, xftfont, cp);
}

static int textwidth(const char *text) {
  if (!text || !*text)
    return 0;
  XGlyphInfo ext;
  XftTextExtentsUtf8(dpy, xftfont, (const FcChar8 *)text, (int)strlen(text), &ext);
  return ext.xOff;
}

static long long nowms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return 0;
  return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

static void updateclock(void) {
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  if (!tm)
    return;
  char datebuf[32];
  char volbuf[48];
  char membuf[32];
  char newcenter[64];
  char newright[96];
  const char *memicon = has_icon_mem ? "󰍛" : "";
  strftime(datebuf, sizeof(datebuf), clockfmt, tm);
  snprintf(newcenter, sizeof(newcenter), "%s", datebuf);
  int hasvol = getvolume(volbuf, sizeof(volbuf));
  int hasram = getusedram(membuf, sizeof(membuf));
  if (hasvol && hasram) {
    if (has_icon_mem)
      snprintf(newright, sizeof(newright), "%s  %s %s", volbuf, memicon, membuf);
    else
      snprintf(newright, sizeof(newright), "%s  %s", volbuf, membuf);
  } else if (hasvol) {
    snprintf(newright, sizeof(newright), "%s", volbuf);
  } else if (hasram) {
    if (has_icon_mem)
      snprintf(newright, sizeof(newright), "%s %s", memicon, membuf);
    else
      snprintf(newright, sizeof(newright), "%s", membuf);
  } else {
    newright[0] = '\0';
  }
  if (strcmp(newcenter, centertext) != 0 || strcmp(newright, righttext) != 0) {
    strncpy(centertext, newcenter, sizeof(centertext) - 1);
    centertext[sizeof(centertext) - 1] = '\0';
    strncpy(righttext, newright, sizeof(righttext) - 1);
    righttext[sizeof(righttext) - 1] = '\0';
    drawbar();
  }
}

static int getvolume(char *buf, size_t buflen) {
  FILE *fp = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@", "r");
  if (!fp)
    return 0;
  char line[256];
  if (!fgets(line, sizeof(line), fp)) {
    pclose(fp);
    return 0;
  }
  pclose(fp);
  float vol = -1.0f;
  if (sscanf(line, "Volume: %f", &vol) != 1)
    return 0;
  int muted = (strstr(line, "[MUTED]") != NULL);
  if (muted) {
    if (has_icon_mute)
      snprintf(buf, buflen, "󰖁 Muted");
    else
      snprintf(buf, buflen, "Muted");
  } else {
    if (has_icon_vol)
      snprintf(buf, buflen, "󰕾 %d%%", (int)(vol * 100.0f + 0.5f));
    else
      snprintf(buf, buflen, "%d%%", (int)(vol * 100.0f + 0.5f));
  }
  return 1;
}

static void grabkeys(void) {
  updatenumlockmask();
  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  for (unsigned int i = 0; i < LENGTH(keys); i++) {
    KeyCode code = XKeysymToKeycode(dpy, keys[i].keysym);
    unsigned int mods[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    for (unsigned int j = 0; j < LENGTH(mods); j++)
      XGrabKey(dpy, code, keys[i].mod | mods[j], root, True, GrabModeAsync, GrabModeAsync);
  }
}

static void updatenumlockmask(void) {
  numlockmask = 0;
  XModifierKeymap *modmap = XGetModifierMapping(dpy);
  KeyCode numlock = XKeysymToKeycode(dpy, XK_Num_Lock);
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < modmap->max_keypermod; j++) {
      if (modmap->modifiermap[i * modmap->max_keypermod + j] == numlock)
        numlockmask = (1u << i);
    }
  }
  XFreeModifiermap(modmap);
}

static void keypress(XEvent *e) {
  XKeyEvent *ev = &e->xkey;
  KeySym sym = XkbKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0, 0);
  for (unsigned int i = 0; i < LENGTH(keys); i++) {
    if (keys[i].keysym == sym
        && (ev->state & ~(numlockmask|LockMask)) == keys[i].mod) {
      if (keys[i].func)
        keys[i].func(&(keys[i].arg));
      if (sym == XF86XK_AudioRaiseVolume || sym == XF86XK_AudioLowerVolume
          || sym == XF86XK_AudioMute || sym == XF86XK_AudioPlay
          || sym == XF86XK_AudioNext || sym == XF86XK_AudioPrev)
        statusdirty = 1;
      if (sym == XF86XK_AudioRaiseVolume || sym == XF86XK_AudioLowerVolume
          || sym == XF86XK_AudioMute) {
        status_refresh_pending = 1;
        status_refresh_at_ms = nowms() + 100;
      }
    }
  }
}

static void killclient(const Arg *arg) {
  (void)arg;
  if (!sel)
    return;
  Atom *protocols = NULL;
  int n = 0;
  if (XGetWMProtocols(dpy, sel->win, &protocols, &n)) {
    for (int i = 0; i < n; i++) {
      if (protocols[i] == wmatom) {
        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = ClientMessage;
        ev.xclient.window = sel->win;
        ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = wmatom;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, sel->win, False, NoEventMask, &ev);
        XFlush(dpy);
        XFree(protocols);
        return;
      }
    }
    XFree(protocols);
  }
  XKillClient(dpy, sel->win);
}

static void promotemaster(const Arg *arg) {
  (void)arg;
  if (!sel || sel == clients)
    return;
  detach(sel);
  attach(sel);
  arrange();
}

static void maprequest(XEvent *e) {
  XMapRequestEvent *ev = &e->xmaprequest;
  XWindowAttributes wa;
  if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
    return;
  if (!wintoclient(ev->window))
    manage(ev->window, &wa);
}

static void configurerequest(XEvent *e) {
  XConfigureRequestEvent *ev = &e->xconfigurerequest;
  XWindowChanges wc;
  Client *c = wintoclient(ev->window);
  if (!c) {
    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
    return;
  }
  arrange();
}

static void destroynotify(XEvent *e) {
  XDestroyWindowEvent *ev = &e->xdestroywindow;
  Client *c = wintoclient(ev->window);
  if (c)
    unmanage(c, 1);
}

static void unmapnotify(XEvent *e) {
  XUnmapEvent *ev = &e->xunmap;
  Client *c = wintoclient(ev->window);
  if (!c)
    return;
  if (c->ignoreunmap > 0) {
    c->ignoreunmap--;
    return;
  }
}

static void quit(const Arg *arg) {
  (void)arg;
  running = 0;
}

static void spawn(const Arg *arg) {
  if (!arg || !arg->v)
    return;
  if (fork() == 0) {
    if (dpy)
      close(ConnectionNumber(dpy));
    setsid();
    execvp(((char *const *)arg->v)[0], (char *const *)arg->v);
    _exit(1);
  }
}

static void setup(void) {
  if (!(dpy = XOpenDisplay(NULL)))
    die("cannot open display");
  XSetErrorHandler(xerror);
  screen = DefaultScreen(dpy);
  root = RootWindow(dpy, screen);
  sw = DisplayWidth(dpy, screen);
  sh = DisplayHeight(dpy, screen);
  wmatom = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  net_wm_window_type_dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
  net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
  net_wm_state_above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);

  gc = XCreateGC(dpy, root, 0, NULL);
  xftfont = XftFontOpenName(dpy, screen, fontname);
  if (!xftfont)
    die("failed to load font");
  has_icon_vol = font_has_glyph("󰕾");
  has_icon_mute = font_has_glyph("󰖁");
  has_icon_mem = font_has_glyph("󰍛");

  Colormap cmap = DefaultColormap(dpy, screen);
  XColor xcol;
  if (!XAllocNamedColor(dpy, cmap, col_bg_hex, &xcol, &xcol))
    die("failed to allocate col_bg");
  col_bg = xcol.pixel;
  if (!XAllocNamedColor(dpy, cmap, col_fg_hex, &xcol, &xcol))
    die("failed to allocate col_fg");
  col_fg = xcol.pixel;
  if (!XAllocNamedColor(dpy, cmap, col_accent_hex, &xcol, &xcol))
    die("failed to allocate col_accent");
  col_accent = xcol.pixel;
  if (!XAllocNamedColor(dpy, cmap, col_border_focus_hex, &xcol, &xcol))
    die("failed to allocate col_border_focus");
  col_border_focus = xcol.pixel;
  if (!XAllocNamedColor(dpy, cmap, col_border_norm_hex, &xcol, &xcol))
    die("failed to allocate col_border_norm");
  col_border_norm = xcol.pixel;

  showbar = topbar;
  if (topbar) {
    XSetWindowAttributes wa;
    wa.override_redirect = True;
    wa.background_pixel = col_bg;
    wa.event_mask = ExposureMask | ButtonPressMask;
    barwin = XCreateWindow(dpy, root, 0, 0, sw, barheight, 0, DefaultDepth(dpy, screen),
                           CopyFromParent, DefaultVisual(dpy, screen),
                           CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
    XChangeProperty(dpy, barwin, net_wm_window_type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&net_wm_window_type_dock, 1);
    XChangeProperty(dpy, barwin, net_wm_state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&net_wm_state_above, 1);
    xftdraw = XftDrawCreate(dpy, barwin, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen));
    if (!xftdraw)
      die("failed to create XftDraw");
    if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), col_fg_hex, &xftcol_fg))
      die("failed to allocate xftcol_fg");
    if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), col_bg_hex, &xftcol_bg))
      die("failed to allocate xftcol_bg");
    if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), col_accent_hex, &xftcol_accent))
      die("failed to allocate xftcol_accent");
    if (showbar)
      XMapRaised(dpy, barwin);
  }

  XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask | StructureNotifyMask | PropertyChangeMask);

  Cursor cursor = XCreateFontCursor(dpy, XC_left_ptr);
  XDefineCursor(dpy, root, cursor);
  if (topbar)
    XDefineCursor(dpy, barwin, cursor);

  grabkeys();
  updateclock();
  drawbar();
  starting = 0;
}

static void togglebar(const Arg *arg) {
  (void)arg;
  if (!topbar)
    return;
  showbar = !showbar;
  if (showbar)
    XMapRaised(dpy, barwin);
  else
    XUnmapWindow(dpy, barwin);
  arrange();
}

static int xerror(Display *dpy, XErrorEvent *ee) {
  (void)dpy;
  if (ee->error_code == BadWindow || ee->error_code == BadDrawable)
    return 0;
  if (ee->error_code == BadAccess) {
    if (starting && (ee->request_code == X_ChangeWindowAttributes ||
        ee->request_code == X_GrabKey ||
        ee->request_code == X_GrabButton))
      die("another window manager is running");
  }
  return 0;
}

static void cleanup(void) {
  if (xftdraw) {
    Visual *vis = DefaultVisual(dpy, screen);
    Colormap cmap = DefaultColormap(dpy, screen);
    XftColorFree(dpy, vis, cmap, &xftcol_fg);
    XftColorFree(dpy, vis, cmap, &xftcol_bg);
    XftColorFree(dpy, vis, cmap, &xftcol_accent);
    XftDrawDestroy(xftdraw);
    xftdraw = NULL;
  }
  if (xftfont) {
    XftFontClose(dpy, xftfont);
    xftfont = NULL;
  }
  if (topbar)
    XDestroyWindow(dpy, barwin);
  if (gc)
    XFreeGC(dpy, gc);
  XCloseDisplay(dpy);
}

static void run(void) {
  XEvent ev;
  int xfd = ConnectionNumber(dpy);
  time_t last = 0;
  while (running) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(xfd, &fds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    select(xfd + 1, &fds, NULL, NULL, &tv);
    while (XPending(dpy)) {
      XNextEvent(dpy, &ev);
      if (topbar && ev.xany.window == barwin && ev.type == Expose)
        drawbar();
      switch (ev.type) {
        case MapRequest: maprequest(&ev); break;
        case ButtonPress: {
          XButtonEvent *bev = &ev.xbutton;
          if (topbar && showbar && bev->window == barwin && bev->button == Button1) {
            for (unsigned int i = 0; i < LENGTH(tags); i++) {
              if (bev->x >= tagx[i] && bev->x < tagx[i] + tagw[i]) {
                Arg a = { .ui = 1u << i };
                view(&a);
                break;
              }
            }
          } else {
            Client *c = wintoclient(bev->window);
            if (c)
              focus(c);
          }
          break;
        }
        case ConfigureRequest: configurerequest(&ev); break;
        case DestroyNotify: destroynotify(&ev); break;
        case UnmapNotify: unmapnotify(&ev); break;
        case KeyPress: keypress(&ev); break;
      }
    }
    time_t now = time(NULL);
    if (now - last >= status_interval) {
      last = now;
      updateclock();
    } else if (statusdirty) {
      statusdirty = 0;
      updateclock();
    } else if (status_refresh_pending) {
      long long ms = nowms();
      if (ms >= status_refresh_at_ms) {
        status_refresh_pending = 0;
        updateclock();
      }
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc > 1) {
    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
      printf("opendwm-1.0\n");
      return 0;
    }
  }
  setup();
  run();
  cleanup();
  return 0;
}
