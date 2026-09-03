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
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
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
  unsigned int mod;
  unsigned int button;
  void (*func)(const Arg *arg);
  const Arg arg;
} Button;

typedef struct {
  const char *class;
  int isfloating;
} Rule;

typedef struct Client Client;
struct Client {
  Window win;
  Window transient_for;
  int x, y, w, h;
  int oldx, oldy, oldw, oldh;
  int oldbw;
  unsigned int tags;
  int ignoreunmap;
  int ismapped;
  int rulefloating;
  int isfloating;
  int oldisfloating;
  int isfullscreen;
  int isdialog;
  int isabove;
  Client *next;
  Client *prev;
  Client *snext;
};

static void arrange(void);
static void attach(Client *c);
static void attachstack(Client *c);
static void applyrules(Client *c);
static void apply_layout(void);
static void apply_fullscreen(void);
static void restack(void);
static void bar_cleanup(void);
static void bar_init(void);
static int bar_is_visible(void);
static void buttonpress(XEvent *e);
static void cleanup(void);
static void configure(Client *c);
static void configurerequest(XEvent *e);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static void drawbar(void);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusstack(const Arg *arg);
static int has_visible_fullscreen(void);
static int is_transient_for_fullscreen(Client *c);
static int is_single_tag(unsigned int mask);
static unsigned int cleanmask(unsigned int mask);
static void grabbuttons(Client *c);
static void grabkeys(void);
static void incmfact(const Arg *arg);
static void incgaps(const Arg *arg);
static void keyevent(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void showhide(Client *c);
static void maprequest(XEvent *e);
static void monocle(void);
static void movemouse(const Arg *arg);
static void movefocus(Client *c);
static void movestack(const Arg *arg);
static void promotemaster(const Arg *arg);
static void quit(const Arg *arg);
static void resize(Client *c, int x, int y, int w, int h);
static void refreshclientrole(Client *c, int initial);
static void run(void);
static void select_visible_focus(void);
static void scan(void);
static int sendevent(Client *c, Atom protocol);
static void setclientfocus(Client *c);
static void setlayout(const Arg *arg);
static void setfullscreen(Client *c, int fullscreen);
static void setwindowstate(Window w, Atom state, int enabled);
static void sigchld(int unused);
static void spawnserial(const Arg *arg);
static void setup(void);
static void spawn(const Arg *arg);
static void startserial(void);
static void reapchildren(void);
static int tag_index_from_mask(unsigned int mask);
static void tile(void);
static void togglebar(const Arg *arg);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatenumlockmask(void);
static void updateclock(void);
static void update_bar_visibility(void);
static int getusedram(char *buf, size_t buflen);
static int getvolume(char *buf, size_t buflen);
static void readvoxtypestatus(void);
static void startvoxtypestatus(void);
static void stopvoxtypestatus(void);
static int textwidth(const char *text);
static long long nowms(void);
static void view(const Arg *arg);
static void tagandview(const Arg *arg);
static Client *nexttiled(Client *c);
static Client *prevtiled(Client *c);
static Client *nextvisible(Client *c);
static Client *prevvisible(Client *c);
static void swapclients(Client *a, Client *b);
static int window_has_state(Window w, Atom state);
static int window_has_type(Window w, Atom type);
static Client *wintoclient(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);

#define TAGMASK ((1u << 10) - 1)
#define LENGTH(X) (sizeof(X) / sizeof((X)[0]))
#define SPAWN_QUEUE_SIZE 64
#define MOUSEMASK (ButtonPressMask|ButtonReleaseMask|PointerMotionMask)
#define TAGKEYS(KEYSYM, TAG) \
  { MODKEY, KEYSYM, view, { .ui = 1u << (TAG) } }, \
  { MODKEY|ShiftMask, KEYSYM, tagandview, { .ui = 1u << (TAG) } }

static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" };

enum { LAYOUT_TILE, LAYOUT_MONOCLE };
enum { VOXTYPE_STOPPED, VOXTYPE_IDLE, VOXTYPE_RECORDING, VOXTYPE_TRANSCRIBING };

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
static XftColor xftcol_dim;
static unsigned long col_bg;
static unsigned long col_fg;
static unsigned long col_accent;
static unsigned long col_border_focus;
static unsigned long col_border_norm;
static Atom wm_delete;
static Atom wm_protocols;
static Atom wm_take_focus;
static Atom net_wm_window_type;
static Atom net_wm_window_type_dialog;
static Atom net_wm_window_type_utility;
static Atom net_wm_window_type_splash;
static Atom net_wm_window_type_dock;
static Atom net_wm_state;
static Atom net_wm_state_above;
static Atom net_wm_state_modal;
static Atom net_wm_state_fullscreen;
static int tagx[10];
static int tagw[10];
static int sw, sh;
static int running = 1;
static int statusdirty = 0;
static int wm_detected = 0;
static unsigned int numlockmask = 0;
static Client *clients = NULL;
static Client *stack = NULL;
static Client *sel = NULL;
static Client *lastsel = NULL;
static Client *tagfocus[10] = { NULL };
static unsigned int tagset = 1;
static int layout = LAYOUT_TILE;
static int showbar = 1;
static char centertext[64] = {0};
static char righttext[96] = {0};
static int has_icon_vol = 0;
static int has_icon_mute = 0;
static int has_icon_mem = 0;
static int has_icon_voxtype = 0;
static int voxtype_state = VOXTYPE_STOPPED;
static int voxtype_fd = -1;
static pid_t voxtype_pid = -1;
static char voxtype_statusbuf[64];
static size_t voxtype_statusbuf_used = 0;
static volatile sig_atomic_t child_exited = 0;
static pid_t serial_pid = -1;
static const void *spawn_queue[SPAWN_QUEUE_SIZE];
static unsigned int spawn_queue_head = 0;
static unsigned int spawn_queue_count = 0;

static void die(const char *msg) {
  fprintf(stderr, "opendwm: %s\n", msg);
  exit(1);
}

static void attach(Client *c) {
  if (!clients) {
    c->next = NULL;
    c->prev = NULL;
    clients = c;
    return;
  }
  c->next = clients->next;
  c->prev = clients;
  if (clients->next)
    clients->next->prev = c;
  clients->next = c;
}

static void attachstack(Client *c) {
  c->snext = stack;
  stack = c;
}

static int client_in_list(Client *c) {
  for (Client *it = clients; it; it = it->next) {
    if (it == c)
      return 1;
  }
  return 0;
}

static void swapclients(Client *a, Client *b) {
  if (!a || !b || a == b)
    return;
  Client *ap = a->prev;
  Client *an = a->next;
  Client *bp = b->prev;
  Client *bn = b->next;
  if (an == b) {
    a->next = bn;
    a->prev = b;
    b->next = a;
    b->prev = ap;
    if (bn)
      bn->prev = a;
    if (ap)
      ap->next = b;
  } else if (bn == a) {
    b->next = an;
    b->prev = a;
    a->next = b;
    a->prev = bp;
    if (an)
      an->prev = b;
    if (bp)
      bp->next = a;
  } else {
    if (ap)
      ap->next = b;
    if (an)
      an->prev = b;
    if (bp)
      bp->next = a;
    if (bn)
      bn->prev = a;
    a->prev = bp;
    a->next = bn;
    b->prev = ap;
    b->next = an;
  }
  if (clients == a)
    clients = b;
  else if (clients == b)
    clients = a;
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

static void detachstack(Client *c) {
  Client **it = &stack;

  while (*it && *it != c)
    it = &(*it)->snext;
  if (*it)
    *it = c->snext;
  c->snext = NULL;
}

static void raisestack(Client *c) {
  if (!c || stack == c)
    return;
  detachstack(c);
  attachstack(c);
}

static Client *wintoclient(Window w) {
  for (Client *c = clients; c; c = c->next)
    if (c->win == w)
      return c;
  return NULL;
}

static void applyrules(Client *c) {
  XClassHint ch = {0};
  c->rulefloating = 0;
  if (!XGetClassHint(dpy, c->win, &ch))
    return;
  for (unsigned int i = 0; rules[i].class; i++) {
    if (ch.res_class && strcmp(rules[i].class, ch.res_class) == 0) {
      c->rulefloating = rules[i].isfloating;
      break;
    }
  }
  if (ch.res_class)
    XFree(ch.res_class);
  if (ch.res_name)
    XFree(ch.res_name);
}

static void synctransienttags(void) {
  unsigned int count = 0;

  for (Client *c = clients; c; c = c->next)
    count++;
  for (unsigned int pass = 0; pass < count; pass++) {
    int changed = 0;
    for (Client *c = clients; c; c = c->next) {
      Client *parent = wintoclient(c->transient_for);
      if (parent && c->tags != parent->tags) {
        c->tags = parent->tags;
        changed = 1;
      }
    }
    if (!changed)
      break;
  }
}

static void refreshclientrole(Client *c, int initial) {
  Window transient = None;
  int isdialog;
  int isabove;
  int wasfloating = c->isfloating;

  XGetTransientForHint(dpy, c->win, &transient);
  isdialog = transient != None
      || window_has_state(c->win, net_wm_state_modal)
      || window_has_type(c->win, net_wm_window_type_dialog)
      || window_has_type(c->win, net_wm_window_type_utility)
      || window_has_type(c->win, net_wm_window_type_splash);
  isabove = window_has_state(c->win, net_wm_state_above);

  c->transient_for = transient;
  c->isdialog = isdialog;
  c->isabove = isabove;
  if (c->isfullscreen)
    c->oldisfloating = c->rulefloating || isdialog || isabove;
  else
    c->isfloating = c->rulefloating || isdialog || isabove;

  if (transient != None) {
    Client *parent = wintoclient(transient);
    if (parent && (initial || c->tags != parent->tags))
      c->tags = parent->tags;
  }
  if (!initial)
    synctransienttags();

  if (!initial && !wasfloating && c->isfloating && !c->isfullscreen) {
    int by = bar_is_visible() ? barheight : 0;
    c->w = c->oldw;
    c->h = c->oldh;
    c->x = (sw - c->w) / 2;
    c->y = (sh - by - c->h) / 2 + by;
    if (c->x < 0)
      c->x = 0;
    if (c->y < by)
      c->y = by;
    resize(c, c->x, c->y, c->w, c->h);
  }
}

static int isvisible(Client *c) {
  return c->tags & tagset;
}

static int has_visible_fullscreen(void) {
  for (Client *c = clients; c; c = c->next) {
    if (isvisible(c) && c->isfullscreen)
      return 1;
  }
  return 0;
}

static int bar_is_visible(void) {
  if (!showbar)
    return 0;
  if (has_visible_fullscreen())
    return 0;
  return 1;
}

static int is_single_tag(unsigned int mask) {
  return mask && !(mask & (mask - 1));
}

static int tag_index_from_mask(unsigned int mask) {
  for (unsigned int i = 0; i < LENGTH(tagfocus); i++) {
    if (mask & (1u << i))
      return (int)i;
  }
  return -1;
}

static unsigned int cleanmask(unsigned int mask) {
  return mask & ~(numlockmask|LockMask);
}

static Client *nexttiled(Client *c) {
  for (; c && (!isvisible(c) || c->isfloating || c->isfullscreen); c = c->next) {
  }
  return c;
}

static Client *prevtiled(Client *c) {
  for (; c && (!isvisible(c) || c->isfloating || c->isfullscreen); c = c->prev) {
  }
  return c;
}

static Client *nextvisible(Client *c) {
  for (; c && !isvisible(c); c = c->next) {
  }
  return c;
}

static Client *prevvisible(Client *c) {
  for (; c && !isvisible(c); c = c->prev) {
  }
  return c;
}

static void movefocus(Client *c) {
  if (!c)
    return;
  focus(c);
}

static void setclientfocus(Client *c) {
  int accepts_input = 1;
  XWMHints *wmh = XGetWMHints(dpy, c->win);

  if (wmh) {
    if (wmh->flags & InputHint)
      accepts_input = wmh->input;
    XFree(wmh);
  }
  if (accepts_input)
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
  if (!sendevent(c, wm_take_focus) && !accepts_input)
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
}

static void focus(Client *c) {
  if (!c || !isvisible(c))
    return;
  if (!c->isfullscreen && !c->isabove && !is_transient_for_fullscreen(c)) {
    for (Client *it = stack; it; it = it->snext) {
      if (isvisible(it) && it->isfullscreen) {
        c = it;
        break;
      }
    }
  }
  XWindowAttributes wa;
  if (!XGetWindowAttributes(dpy, c->win, &wa) || wa.map_state != IsViewable)
    return;
  if (sel && sel != c)
    lastsel = sel;
  sel = c;
  raisestack(c);
  if (!c->isdialog) {
    for (unsigned int i = 0; i < LENGTH(tagfocus); i++) {
      unsigned int mask = 1u << i;
      if (c->tags & mask)
        tagfocus[i] = c;
    }
  }
  setclientfocus(c);
  for (Client *it = clients; it; it = it->next) {
    if (!isvisible(it))
      continue;
    if (it->isfullscreen) {
      XSetWindowBorderWidth(dpy, it->win, 0);
    } else if (layout == LAYOUT_MONOCLE) {
      XSetWindowBorderWidth(dpy, it->win, 0);
    } else {
      XSetWindowBorderWidth(dpy, it->win, borderpx);
      XSetWindowBorder(dpy, it->win, (it == sel) ? col_border_focus : col_border_norm);
    }
  }
  drawbar();
  restack();
}

static void focusin(XEvent *e) {
  XFocusChangeEvent *ev = &e->xfocus;

  if (sel && ev->window != sel->win)
    setclientfocus(sel);
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
  ce.border_width = (layout == LAYOUT_MONOCLE || c->isfullscreen) ? 0 : borderpx;
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
  if (w < 1)
    w = 1;
  if (h < 1)
    h = 1;
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
  c->oldx = c->x;
  c->oldy = c->y;
  c->oldw = c->w;
  c->oldh = c->h;
  c->oldbw = wa->border_width;
  c->tags = tagset;
  applyrules(c);
  c->isfullscreen = window_has_state(w, net_wm_state_fullscreen);
  refreshclientrole(c, 1);
  if (!c->isfullscreen)
    c->oldisfloating = c->isfloating;
  if (c->isfloating && !c->isfullscreen) {
    int by = (bar_is_visible() ? barheight : 0);
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
  attachstack(c);
  XSelectInput(dpy, w, ButtonPressMask | EnterWindowMask | FocusChangeMask | PropertyChangeMask | StructureNotifyMask);
  grabbuttons(c);
  XSetWindowBorderWidth(dpy, w, (layout == LAYOUT_MONOCLE || c->isfullscreen) ? 0 : borderpx);
  XSetWindowBorder(dpy, w, col_border_norm);
  XMapWindow(dpy, w);
  c->ismapped = 1;
  focus(c);
  arrange();
}

static void unmanage(Client *c, int destroyed) {
  if (!c)
    return;
  Client *focus_c = NULL;
  if (sel == c) {
    Client *parent = wintoclient(c->transient_for);
    if (parent && isvisible(parent)) {
      XWindowAttributes wa;
      if (XGetWindowAttributes(dpy, parent->win, &wa) && wa.map_state == IsViewable)
        focus_c = parent;
    }
    if (!focus_c && lastsel && lastsel != c && client_in_list(lastsel) && isvisible(lastsel)) {
      XWindowAttributes wa;
      if (XGetWindowAttributes(dpy, lastsel->win, &wa) && wa.map_state == IsViewable)
        focus_c = lastsel;
    }
    if (!focus_c) {
      focus_c = nextvisible(c->next);
      if (!focus_c)
        focus_c = prevvisible(c->prev);
    }
  }
  if (!destroyed) {
    XGrabServer(dpy);
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    XSelectInput(dpy, c->win, NoEventMask);
    XSetWindowBorderWidth(dpy, c->win, c->oldbw);
    XUngrabServer(dpy);
  }
  detach(c);
  detachstack(c);
  for (unsigned int i = 0; i < LENGTH(tagfocus); i++) {
    if (tagfocus[i] == c)
      tagfocus[i] = NULL;
  }
  if (sel == c)
    sel = focus_c;
  if (lastsel == c)
    lastsel = NULL;
  free(c);
  select_visible_focus();
  if (sel)
    focus(sel);
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
  if (!sel || sel->isfloating)
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
  if (maxfact < minfact)
    maxfact = minfact;
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
  select_visible_focus();
  arrange();
}

static void tagandview(const Arg *arg) {
  if ((arg->ui & TAGMASK) == 0)
    return;
  if (sel) {
    sel->tags = arg->ui & TAGMASK;
    synctransienttags();
  }
  tagset = arg->ui & TAGMASK;
  select_visible_focus();
  arrange();
}

static void tile(void) {
  unsigned int n = 0;
  for (Client *c = nexttiled(clients); c; c = nexttiled(c->next))
    n++;
  if (n == 0)
    return;

  int wx = 0;
  int wy = bar_is_visible() ? (int)barheight : 0;
  if (wy >= sh)
    wy = sh > 1 ? sh - 1 : 0;
  int ww = sw;
  int wh = sh - wy;

  int g = gappx;
  int maxg = ((ww < wh ? ww : wh) - 1) / 2;
  if (maxg < 0)
    maxg = 0;
  if (g > maxg)
    g = maxg;
  wx += g;
  wy += g;
  ww -= 2 * g;
  wh -= 2 * g;
  if (ww < 1)
    ww = 1;
  if (wh < 1)
    wh = 1;

  unsigned int mcount = (n < nmaster) ? n : nmaster;
  unsigned int scount = (n > nmaster) ? (n - nmaster) : 0;

  int mw = ww;
  int swidth = 0;
  int columns_overlap = 0;
  if (mcount == 0) {
    mw = 0;
    swidth = ww;
  } else if (scount > 0) {
    int column_gap = (ww >= 3) ? g : 0;
    int available = ww - column_gap;
    if (available < 2) {
      mw = ww;
      swidth = ww;
      g = 0;
      columns_overlap = 1;
    } else {
      mw = (int)(available * mfact);
      if (minmasterw + minstackw <= available) {
        if (mw < minmasterw)
          mw = minmasterw;
        if (available - mw < minstackw)
          mw = available - minstackw;
      }
      if (mw < 1)
        mw = 1;
      if (mw >= available)
        mw = available - 1;
      swidth = available - mw;
      g = column_gap;
    }
  }

  int mx = wx;
  int sx = (mcount == 0 || columns_overlap) ? wx : wx + mw + g;
  int my = wy;
  int sy = wy;
  int mgap = g;
  int sgap = g;
  if (mcount > 1 && (int)(mgap * (mcount - 1)) > wh - (int)mcount)
    mgap = (wh > (int)mcount) ? (wh - (int)mcount) / (int)(mcount - 1) : 0;
  if (scount > 1 && (int)(sgap * (scount - 1)) > wh - (int)scount)
    sgap = (wh > (int)scount) ? (wh - (int)scount) / (int)(scount - 1) : 0;

  unsigned int i = 0;
  for (Client *c = nexttiled(clients); c; c = nexttiled(c->next), i++) {
    if (i < mcount) {
      int mh = (wh - (int)(mgap * (mcount - 1))) / (int)mcount;
      int mrem = (wh - (int)(mgap * (mcount - 1))) % (int)mcount;
      int ch = mh + ((int)i == (int)mcount - 1 ? mrem : 0);
      resize(c, mx, my, mw - 2 * (int)borderpx, ch - 2 * (int)borderpx);
      my += ch + mgap;
    } else {
      unsigned int si = i - mcount;
      int shh = (wh - (int)(sgap * (scount - 1))) / (int)scount;
      int srem = (wh - (int)(sgap * (scount - 1))) % (int)scount;
      int ch = shh + ((int)si == (int)scount - 1 ? srem : 0);
      resize(c, sx, sy, swidth - 2 * (int)borderpx, ch - 2 * (int)borderpx);
      sy += ch + sgap;
    }
  }
}

static void monocle(void) {
  int wx = 0;
  int wy = bar_is_visible() ? barheight : 0;
  int ww = sw;
  int wh = sh - (bar_is_visible() ? barheight : 0);
  for (Client *c = clients; c; c = c->next) {
    if (!isvisible(c) || c->isfloating || c->isfullscreen)
      continue;
    XSetWindowBorderWidth(dpy, c->win, 0);
    resize(c, wx, wy, ww, wh);
  }
}

static void movemouse(const Arg *arg) {
  (void)arg;
  if (!sel || !sel->isfloating || sel->isfullscreen)
    return;

  Client *c = sel;
  Window dummy;
  int startx = c->x;
  int starty = c->y;
  int xroot, yroot, dummy_i;
  unsigned int dummy_ui;

  if (!XQueryPointer(dpy, root, &dummy, &dummy, &xroot, &yroot,
                     &dummy_i, &dummy_i, &dummy_ui))
    return;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                   None, None, CurrentTime) != GrabSuccess)
    return;

  for (;;) {
    XEvent ev;
    XMaskEvent(dpy, MOUSEMASK, &ev);
    if (ev.type == MotionNotify) {
      resize(c, startx + ev.xmotion.x_root - xroot,
             starty + ev.xmotion.y_root - yroot, c->w, c->h);
    } else if (ev.type == ButtonRelease) {
      break;
    }
  }
  XUngrabPointer(dpy, CurrentTime);
  restack();
}

static void showhide(Client *c) {
  if (!c)
    return;
  if (isvisible(c)) {
    XMoveWindow(dpy, c->win, c->x, c->y);
    showhide(c->next);
  } else {
    showhide(c->next);
    XMoveWindow(dpy, c->win, -(c->w + 2 * borderpx) * 2, c->y);
  }
}

static void apply_layout(void) {
  if (layout == LAYOUT_MONOCLE)
    monocle();
  else
    tile();
  if (topbar && bar_is_visible())
    XRaiseWindow(dpy, barwin);
}

static void apply_fullscreen(void) {
  for (Client *c = clients; c; c = c->next) {
    if (!isvisible(c) || !c->isfullscreen)
      continue;
    XSetWindowBorderWidth(dpy, c->win, 0);
    resize(c, 0, 0, sw, sh);
    XRaiseWindow(dpy, c->win);
  }
}

static int is_transient_for_fullscreen(Client *c) {
  Window owner = c ? c->transient_for : None;

  for (unsigned int depth = 0; owner != None && depth < 32; depth++) {
    Client *parent = wintoclient(owner);
    if (!parent)
      return 0;
    if (isvisible(parent) && parent->isfullscreen)
      return 1;
    owner = parent->transient_for;
  }
  return 0;
}

static void restack(void) {
  unsigned int count = (topbar && bar_is_visible()) ? 1 : 0;
  unsigned int i = 0;

  for (Client *c = stack; c; c = c->snext)
    if (isvisible(c))
      count++;
  if (count == 0)
    return;

  Window *wins = calloc(count, sizeof(Window));
  if (!wins)
    return;
  if (topbar && bar_is_visible())
    wins[i++] = barwin;
  for (Client *c = stack; c; c = c->snext)
    if (isvisible(c) && c->isabove)
      wins[i++] = c->win;
  for (Client *c = stack; c; c = c->snext)
    if (isvisible(c) && !c->isabove && !c->isfullscreen
        && is_transient_for_fullscreen(c))
      wins[i++] = c->win;
  for (Client *c = stack; c; c = c->snext)
    if (isvisible(c) && !c->isabove && c->isfullscreen)
      wins[i++] = c->win;
  for (Client *c = stack; c; c = c->snext)
    if (isvisible(c) && !c->isabove && !c->isfullscreen
        && !is_transient_for_fullscreen(c) && c->isfloating)
      wins[i++] = c->win;
  for (Client *c = stack; c; c = c->snext)
    if (isvisible(c) && !c->isabove && !c->isfullscreen
        && !is_transient_for_fullscreen(c) && !c->isfloating)
      wins[i++] = c->win;

  if (i > 1)
    XRestackWindows(dpy, wins, (int)i);
  else
    XRaiseWindow(dpy, wins[0]);
  free(wins);
}

static void select_visible_focus(void) {
  if (sel && !isvisible(sel))
    sel = NULL;
  if (!sel && is_single_tag(tagset)) {
    int tag = tag_index_from_mask(tagset);
    if (tag >= 0 && tag < (int)LENGTH(tagfocus)) {
      Client *c = tagfocus[tag];
      if (c && isvisible(c))
        sel = c;
    }
  }
  if (!sel)
    sel = nextvisible(clients);
}

static void arrange(void) {
  showhide(clients);
  select_visible_focus();
  apply_layout();
  apply_fullscreen();
  update_bar_visibility();
  if (sel)
    focus(sel);
  else {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    restack();
  }
  drawbar();
}

static void drawbar(void) {
  if (!topbar || !bar_is_visible())
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

  const char *voxtypetext = NULL;
  XftColor *voxtypecolor = &xftcol_accent;
  const char *voxtype_idle = has_icon_voxtype ? "" : "[MIC]";
  const char *voxtype_recording = has_icon_voxtype ? "" : "[REC]";
  const char *voxtype_transcribing = has_icon_voxtype ? "" : "[...]";
  if (voxtype_state == VOXTYPE_IDLE) {
    voxtypetext = voxtype_idle;
    voxtypecolor = &xftcol_dim;
  } else if (voxtype_state == VOXTYPE_RECORDING) {
    voxtypetext = voxtype_recording;
  } else if (voxtype_state == VOXTYPE_TRANSCRIBING) {
    voxtypetext = voxtype_transcribing;
  }

  int statustextwidth = textwidth(righttext);
  int voxtypeslotwidth = 0;
  int voxtypegap = 0;
  if (voxtypetext) {
    voxtypeslotwidth = textwidth(voxtype_idle);
    int w = textwidth(voxtype_recording);
    if (w > voxtypeslotwidth)
      voxtypeslotwidth = w;
    w = textwidth(voxtype_transcribing);
    if (w > voxtypeslotwidth)
      voxtypeslotwidth = w;
    if (statustextwidth > 0)
      voxtypegap = textwidth("  ");
  }
  int rightwidth = voxtypeslotwidth + voxtypegap + statustextwidth;
  int rightx = sw - rightwidth - 10;
  if (rightx < x)
    rightx = x + 10;
  if (voxtypetext) {
    int voxtypewidth = textwidth(voxtypetext);
    int voxtypex = rightx + (voxtypeslotwidth - voxtypewidth) / 2;
    XftDrawStringUtf8(xftdraw, voxtypecolor, xftfont, voxtypex,
                      (barheight + xftfont->ascent - xftfont->descent) / 2,
                      (const FcChar8 *)voxtypetext, (int)strlen(voxtypetext));
  }
  if (statustextwidth > 0) {
    int statusx = rightx + voxtypeslotwidth + voxtypegap;
    XftDrawStringUtf8(xftdraw, &xftcol_fg, xftfont, statusx,
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
  datebuf[0] = '\0';
  if (strftime(datebuf, sizeof(datebuf), clockfmt, tm) == 0)
    snprintf(datebuf, sizeof(datebuf), "invalid clock format");
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
  int pipefd[2];
  if (pipe(pipefd) < 0)
    return 0;
  char line[256];
  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    if (dpy)
      close(ConnectionNumber(dpy));
    if (dup2(pipefd[1], STDOUT_FILENO) < 0)
      _exit(1);
    close(pipefd[1]);
    execlp("wpctl", "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL);
    _exit(1);
  }
  close(pipefd[1]);
  if (pid < 0) {
    close(pipefd[0]);
    return 0;
  }

  size_t used = 0;
  int pipe_open = 1;
  int child_done = 0;
  long long deadline = nowms() + 1000;
  while (!child_done || pipe_open) {
    if (!child_done && waitpid(pid, NULL, WNOHANG) == pid)
      child_done = 1;
    long long remaining = deadline - nowms();
    if (!child_done && remaining <= 0) {
      kill(pid, SIGKILL);
      while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
        ;
      child_done = 1;
    }

    if (remaining < 0)
      remaining = 0;
    struct pollfd pfd = {
      .fd = pipefd[0],
      .events = POLLIN
    };
    int ready = poll(pipe_open ? &pfd : NULL, pipe_open ? 1 : 0,
                     child_done ? 0 : (int)remaining);
    if (pipe_open && ready > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
      ssize_t nread = read(pipefd[0], line + used, sizeof(line) - 1 - used);
      if (nread > 0)
        used += (size_t)nread;
      else if (nread == 0 || (errno != EINTR && errno != EAGAIN)) {
        close(pipefd[0]);
        pipe_open = 0;
      }
    }
    if (pipe_open && ready > 0 && (pfd.revents & (POLLERR | POLLNVAL))) {
      close(pipefd[0]);
      pipe_open = 0;
    }
    if (child_done && ready <= 0 && pipe_open) {
      close(pipefd[0]);
      pipe_open = 0;
    }
  }
  if (pipe_open)
    close(pipefd[0]);
  line[used] = '\0';
  if (used == 0)
    return 0;

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

static void setvoxtypestatus(const char *status) {
  int state = VOXTYPE_STOPPED;

  if (strcmp(status, "idle") == 0)
    state = VOXTYPE_IDLE;
  else if (strcmp(status, "recording") == 0)
    state = VOXTYPE_RECORDING;
  else if (strcmp(status, "transcribing") == 0)
    state = VOXTYPE_TRANSCRIBING;
  if (state != voxtype_state) {
    voxtype_state = state;
    drawbar();
  }
}

static void stopvoxtypestatus(void) {
  if (voxtype_fd >= 0) {
    close(voxtype_fd);
    voxtype_fd = -1;
  }
  if (voxtype_pid > 0)
    kill(voxtype_pid, SIGTERM);
  voxtype_statusbuf_used = 0;
  setvoxtypestatus("stopped");
}

static void readvoxtypestatus(void) {
  char buf[64];
  ssize_t nread = read(voxtype_fd, buf, sizeof(buf));

  if (nread <= 0) {
    if (nread == 0 || (errno != EINTR && errno != EAGAIN))
      stopvoxtypestatus();
    return;
  }
  for (ssize_t i = 0; i < nread; i++) {
    if (buf[i] == '\n') {
      if (voxtype_statusbuf_used > 0
          && voxtype_statusbuf[voxtype_statusbuf_used - 1] == '\r')
        voxtype_statusbuf_used--;
      voxtype_statusbuf[voxtype_statusbuf_used] = '\0';
      setvoxtypestatus(voxtype_statusbuf);
      voxtype_statusbuf_used = 0;
    } else if (voxtype_statusbuf_used + 1 < sizeof(voxtype_statusbuf)) {
      voxtype_statusbuf[voxtype_statusbuf_used++] = buf[i];
    } else {
      voxtype_statusbuf_used = 0;
    }
  }
}

static void startvoxtypestatus(void) {
  int pipefd[2];

  if (pipe(pipefd) < 0)
    return;
  int flags = fcntl(pipefd[0], F_GETFL);
  if (flags < 0 || fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) < 0
      || fcntl(pipefd[0], F_SETFD, FD_CLOEXEC) < 0
      || fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return;
  }
  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    if (dpy)
      close(ConnectionNumber(dpy));
    if (dup2(pipefd[1], STDOUT_FILENO) < 0)
      _exit(1);
    close(pipefd[1]);
    execlp("voxtype", "voxtype", "status", "--follow", "--format", "text", NULL);
    _exit(1);
  }
  close(pipefd[1]);
  if (pid < 0) {
    close(pipefd[0]);
    return;
  }
  voxtype_fd = pipefd[0];
  voxtype_pid = pid;
}

static void grabbuttons(Client *c) {
  unsigned int mods[] = { 0, LockMask, numlockmask, numlockmask|LockMask };

  XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
  for (unsigned int i = 0; i < LENGTH(buttons); i++) {
    for (unsigned int j = 0; j < LENGTH(mods); j++) {
      XGrabButton(dpy, buttons[i].button, buttons[i].mod | mods[j], c->win,
                  False, ButtonPressMask, GrabModeAsync, GrabModeAsync,
                  None, None);
    }
  }
}

static void grabkeys(void) {
  const Key *keysets[] = { keys, releasekeys };
  const size_t keycounts[] = { LENGTH(keys), LENGTH(releasekeys) };

  updatenumlockmask();
  XGrabServer(dpy);
  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  for (size_t set = 0; set < LENGTH(keysets); set++) {
    for (size_t i = 0; i < keycounts[set]; i++) {
      KeyCode code = XKeysymToKeycode(dpy, keysets[set][i].keysym);
      if (code == 0) {
        fprintf(stderr, "opendwm: cannot grab unmapped keysym 0x%lx\n",
                (unsigned long)keysets[set][i].keysym);
        continue;
      }
      unsigned int mods[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
      for (unsigned int j = 0; j < LENGTH(mods); j++)
        XGrabKey(dpy, code, keysets[set][i].mod | mods[j], root, True,
                 GrabModeAsync, GrabModeAsync);
    }
  }
  XUngrabServer(dpy);
  XSync(dpy, False);
}

static void updatenumlockmask(void) {
  numlockmask = 0;
  XModifierKeymap *modmap = XGetModifierMapping(dpy);
  if (!modmap)
    return;
  KeyCode numlock = XKeysymToKeycode(dpy, XK_Num_Lock);
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < modmap->max_keypermod; j++) {
      if (modmap->modifiermap[i * modmap->max_keypermod + j] == numlock)
        numlockmask = (1u << i);
    }
  }
  XFreeModifiermap(modmap);
}

static void keyevent(XEvent *e) {
  XKeyEvent *ev = &e->xkey;
  KeySym sym = XkbKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0, 0);
  const Key *keyset = e->type == KeyRelease ? releasekeys : keys;
  size_t keycount = e->type == KeyRelease ? LENGTH(releasekeys) : LENGTH(keys);

  for (size_t i = 0; i < keycount; i++) {
    if (keyset[i].keysym == sym && cleanmask(ev->state) == keyset[i].mod) {
      if (e->type == KeyRelease
          && XEventsQueued(dpy, QueuedAfterReading) > 0) {
        XEvent next;

        XPeekEvent(dpy, &next);
        if (next.type == KeyPress && next.xkey.keycode == ev->keycode
            && next.xkey.time == ev->time) {
          XNextEvent(dpy, &next);
          return;
        }
      }
      if (keyset[i].func)
        keyset[i].func(&(keyset[i].arg));
      break;
    }
  }
}

static void buttonpress(XEvent *e) {
  XButtonEvent *ev = &e->xbutton;
  int handled = 0;

  if (topbar && showbar && ev->window == barwin && ev->button == Button1) {
    for (unsigned int i = 0; i < LENGTH(tags); i++) {
      if (ev->x >= tagx[i] && ev->x < tagx[i] + tagw[i]) {
        Arg a = { .ui = 1u << i };
        view(&a);
        handled = 1;
        break;
      }
    }
  } else {
    Window win = (ev->window == root) ? ev->subwindow : ev->window;
    Client *c = (win != None) ? wintoclient(win) : NULL;
    if (c && ev->button >= Button1 && ev->button <= Button3 && c != sel)
      focus(c);
    if (c) {
      for (unsigned int i = 0; i < LENGTH(buttons); i++) {
        if (buttons[i].button == ev->button
            && cleanmask(ev->state) == buttons[i].mod) {
          if (buttons[i].func)
            buttons[i].func(&(buttons[i].arg));
          handled = 1;
          break;
        }
      }
    }
  }

  XAllowEvents(dpy, handled ? AsyncPointer : ReplayPointer, CurrentTime);
}

static void killclient(const Arg *arg) {
  (void)arg;
  if (!sel)
    return;
  if (sendevent(sel, wm_delete))
    return;
  XKillClient(dpy, sel->win);
}

static void promotemaster(const Arg *arg) {
  (void)arg;
  if (!sel || sel->isfloating || sel == clients)
    return;
  swapclients(clients, sel);
  arrange();
}

static void maprequest(XEvent *e) {
  XMapRequestEvent *ev = &e->xmaprequest;
  XWindowAttributes wa;
  if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
    return;
  if (window_has_type(ev->window, net_wm_window_type_dock)) {
    XMapWindow(dpy, ev->window);
    return;
  }
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

  if (c->isfloating && !c->isfullscreen) {
    if (ev->value_mask & CWX)
      c->x = ev->x;
    if (ev->value_mask & CWY)
      c->y = ev->y;
    if (ev->value_mask & CWWidth)
      c->w = ev->width;
    if (ev->value_mask & CWHeight)
      c->h = ev->height;
    int x = c->x;
    if (!isvisible(c))
      x = -(c->w + 2 * (int)borderpx) * 2;
    XMoveResizeWindow(dpy, c->win, x, c->y, c->w, c->h);
    configure(c);
    restack();
    return;
  }

  configure(c);
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
    c->ismapped = 0;
    return;
  }
  c->ismapped = 0;
  unmanage(c, 0);
}

static void quit(const Arg *arg) {
  (void)arg;
  running = 0;
}

static void spawn(const Arg *arg) {
  if (!arg || !arg->v)
    return;
  pid_t pid = fork();
  if (pid == 0) {
    if (dpy)
      close(ConnectionNumber(dpy));
    setsid();
    execvp(((char *const *)arg->v)[0], (char *const *)arg->v);
    _exit(1);
  }
  if (pid < 0)
    fprintf(stderr, "opendwm: fork failed: %s\n", strerror(errno));
}

static pid_t spawncommand(const void *command) {
  pid_t pid = fork();

  if (pid == 0) {
    if (dpy)
      close(ConnectionNumber(dpy));
    setsid();
    execvp(((char *const *)command)[0], (char *const *)command);
    _exit(1);
  }
  if (pid < 0)
    fprintf(stderr, "opendwm: fork failed: %s\n", strerror(errno));
  return pid;
}

static void startserial(void) {
  while (serial_pid < 0 && spawn_queue_count > 0) {
    const void *command = spawn_queue[spawn_queue_head];
    spawn_queue_head = (spawn_queue_head + 1) % SPAWN_QUEUE_SIZE;
    spawn_queue_count--;
    serial_pid = spawncommand(command);
  }
  if (serial_pid < 0 && spawn_queue_count == 0) {
    statusdirty = 1;
  }
}

static void spawnserial(const Arg *arg) {
  if (!arg || !arg->v)
    return;
  if (spawn_queue_count == SPAWN_QUEUE_SIZE) {
    fprintf(stderr, "opendwm: serial command queue is full\n");
    return;
  }
  unsigned int tail = (spawn_queue_head + spawn_queue_count) % SPAWN_QUEUE_SIZE;
  spawn_queue[tail] = arg->v;
  spawn_queue_count++;
  startserial();
}

static void reapchildren(void) {
  int status;
  pid_t pid;

  if (!child_exited)
    return;
  child_exited = 0;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (pid == serial_pid) {
      serial_pid = -1;
      startserial();
    } else if (pid == voxtype_pid) {
      voxtype_pid = -1;
    }
  }
}

static void sigchld(int unused) {
  (void)unused;
  child_exited = 1;
}

static int sendevent(Client *c, Atom protocol) {
  Atom *protocols = NULL;
  int count = 0;
  int supported = 0;

  if (XGetWMProtocols(dpy, c->win, &protocols, &count)) {
    for (int i = 0; i < count; i++) {
      if (protocols[i] == protocol) {
        supported = 1;
        break;
      }
    }
    XFree(protocols);
  }
  if (!supported)
    return 0;

  XEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = ClientMessage;
  ev.xclient.window = c->win;
  ev.xclient.message_type = wm_protocols;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = protocol;
  ev.xclient.data.l[1] = CurrentTime;
  XSendEvent(dpy, c->win, False, NoEventMask, &ev);
  return 1;
}

static void setup(void) {
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigchld;
  sa.sa_flags = SA_NOCLDSTOP;
  if (sigemptyset(&sa.sa_mask) == -1)
    die("sigemptyset failed");
  if (sigaction(SIGCHLD, &sa, NULL) == -1)
    die("sigaction failed");

  if (!(dpy = XOpenDisplay(NULL)))
    die("cannot open display");
  XSetErrorHandler(xerror);
  screen = DefaultScreen(dpy);
  root = RootWindow(dpy, screen);
  sw = DisplayWidth(dpy, screen);
  sh = DisplayHeight(dpy, screen);
  XSetErrorHandler(xerrorstart);
  XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask
      | ButtonPressMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask
      | StructureNotifyMask | PropertyChangeMask);
  XSync(dpy, False);
  if (wm_detected)
    die("another window manager is running");
  XSetErrorHandler(xerror);
  wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
  wm_take_focus = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
  net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  net_wm_window_type_dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  net_wm_window_type_utility = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
  net_wm_window_type_splash = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
  net_wm_window_type_dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
  net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
  net_wm_state_above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
  net_wm_state_modal = XInternAtom(dpy, "_NET_WM_STATE_MODAL", False);
  net_wm_state_fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);

  gc = XCreateGC(dpy, root, 0, NULL);
  xftfont = XftFontOpenName(dpy, screen, fontname);
  if (!xftfont)
    die("failed to load font");
  has_icon_vol = font_has_glyph("󰕾");
  has_icon_mute = font_has_glyph("󰖁");
  has_icon_mem = font_has_glyph("󰍛");
  has_icon_voxtype = font_has_glyph("") && font_has_glyph("");

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
  bar_init();

  for (unsigned int button = Button1; button <= Button3; button++)
    XGrabButton(dpy, button, AnyModifier, root, True, ButtonPressMask,
                GrabModeSync, GrabModeAsync, None, None);

  Cursor cursor = XCreateFontCursor(dpy, XC_left_ptr);
  XDefineCursor(dpy, root, cursor);
  if (topbar)
    XDefineCursor(dpy, barwin, cursor);

  grabkeys();
  scan();
  if (topbar)
    startvoxtypestatus();
  updateclock();
  drawbar();
}

static void togglebar(const Arg *arg) {
  (void)arg;
  if (!topbar)
    return;
  showbar = !showbar;
  update_bar_visibility();
  arrange();
}

static void update_bar_visibility(void) {
  if (!topbar)
    return;
  if (bar_is_visible())
    XMapRaised(dpy, barwin);
  else
    XUnmapWindow(dpy, barwin);
}

static int window_has_state(Window w, Atom state) {
  Atom actual;
  int format;
  unsigned long nitems, bytes_after;
  Atom *props = NULL;
  int found = 0;
  if (XGetWindowProperty(dpy, w, net_wm_state, 0, 32, False, XA_ATOM,
                         &actual, &format, &nitems, &bytes_after,
                         (unsigned char **)&props) != Success)
    return 0;
  if (actual != XA_ATOM || format != 32) {
    if (props)
      XFree(props);
    return 0;
  }
  if (props) {
    for (unsigned long i = 0; i < nitems; i++) {
      if (props[i] == state) {
        found = 1;
        break;
      }
    }
    XFree(props);
  }
  return found;
}

static int window_has_type(Window w, Atom type) {
  Atom actual;
  int format;
  unsigned long nitems, bytes_after;
  Atom *props = NULL;
  int found = 0;
  if (XGetWindowProperty(dpy, w, net_wm_window_type, 0, 32, False, XA_ATOM,
                         &actual, &format, &nitems, &bytes_after,
                         (unsigned char **)&props) != Success)
    return 0;
  if (actual != XA_ATOM || format != 32) {
    if (props)
      XFree(props);
    return 0;
  }
  if (props) {
    for (unsigned long i = 0; i < nitems; i++) {
      if (props[i] == type) {
        found = 1;
        break;
      }
    }
    XFree(props);
  }
  return found;
}

static void setwindowstate(Window w, Atom state, int enabled) {
  Atom actual = None;
  int format = 0;
  unsigned long nitems = 0;
  unsigned long bytes_after = 0;
  Atom *props = NULL;

  if (XGetWindowProperty(dpy, w, net_wm_state, 0, 1024, False, XA_ATOM,
                         &actual, &format, &nitems, &bytes_after,
                         (unsigned char **)&props) != Success)
    return;
  if (actual != XA_ATOM || format != 32) {
    if (props)
      XFree(props);
    props = NULL;
    nitems = 0;
  }

  unsigned long found = nitems;
  for (unsigned long i = 0; i < nitems; i++) {
    if (props[i] == state) {
      found = i;
      break;
    }
  }
  if ((enabled && found < nitems) || (!enabled && found == nitems)) {
    if (props)
      XFree(props);
    return;
  }

  if (enabled) {
    Atom *next = malloc((nitems + 1) * sizeof(Atom));
    if (!next) {
      if (props)
        XFree(props);
      return;
    }
    if (nitems > 0)
      memcpy(next, props, nitems * sizeof(Atom));
    next[nitems++] = state;
    XChangeProperty(dpy, w, net_wm_state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)next, (int)nitems);
    free(next);
  } else {
    for (unsigned long i = found + 1; i < nitems; i++)
      props[i - 1] = props[i];
    nitems--;
    XChangeProperty(dpy, w, net_wm_state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)props, (int)nitems);
  }
  if (props)
    XFree(props);
}

static void setfullscreen(Client *c, int fullscreen) {
  if (!c)
    return;
  setwindowstate(c->win, net_wm_state_fullscreen, fullscreen);
  if (c->isfullscreen == fullscreen)
    return;

  if (fullscreen) {
    c->oldx = c->x;
    c->oldy = c->y;
    c->oldw = c->w;
    c->oldh = c->h;
    c->oldisfloating = c->isfloating;
    c->isfullscreen = 1;
    XSetWindowBorderWidth(dpy, c->win, 0);
    XRaiseWindow(dpy, c->win);
  } else {
    c->isfullscreen = 0;
    c->isfloating = c->oldisfloating;
    c->x = c->oldx;
    c->y = c->oldy;
    c->w = c->oldw;
    c->h = c->oldh;
    if (c->isfloating)
      resize(c, c->x, c->y, c->w, c->h);
  }
  arrange();
}

static void scan(void) {
  Window dummy1, dummy2, *wins = NULL;
  unsigned int num = 0;
  XWindowAttributes wa;

  if (!XQueryTree(dpy, root, &dummy1, &dummy2, &wins, &num))
    return;

  for (unsigned int i = 0; i < num; i++) {
    if (!XGetWindowAttributes(dpy, wins[i], &wa) || wa.override_redirect)
      continue;
    if (window_has_type(wins[i], net_wm_window_type_dock))
      continue;
    if (XGetTransientForHint(dpy, wins[i], &dummy1))
      continue;
    if (wa.map_state == IsViewable)
      manage(wins[i], &wa);
  }

  for (unsigned int i = 0; i < num; i++) {
    if (!XGetWindowAttributes(dpy, wins[i], &wa) || wa.override_redirect)
      continue;
    if (window_has_type(wins[i], net_wm_window_type_dock))
      continue;
    if (!XGetTransientForHint(dpy, wins[i], &dummy1))
      continue;
    if (wa.map_state == IsViewable)
      manage(wins[i], &wa);
  }

  if (wins)
    XFree(wins);
}

static int xerror(Display *dpy, XErrorEvent *ee) {
  if (ee->error_code == BadWindow || ee->error_code == BadDrawable)
    return 0;
  char message[128];
  XGetErrorText(dpy, ee->error_code, message, sizeof(message));
  fprintf(stderr, "opendwm: X error: %s (request %u, minor %u)\n",
          message, ee->request_code, ee->minor_code);
  return 0;
}

static int xerrorstart(Display *dpy, XErrorEvent *ee) {
  (void)dpy;
  if (ee->error_code == BadAccess && ee->request_code == X_ChangeWindowAttributes)
    wm_detected = 1;
  return 0;
}

static void cleanup(void) {
  stopvoxtypestatus();
  while (clients) {
    Client *c = clients;
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    XSetWindowBorderWidth(dpy, c->win, c->oldbw);
    XMoveWindow(dpy, c->win, c->x, c->y);
    detach(c);
    detachstack(c);
    free(c);
  }
  bar_cleanup();
  if (gc)
    XFreeGC(dpy, gc);
  XCloseDisplay(dpy);
}

static void bar_init(void) {
  if (!topbar)
    return;
  XSetWindowAttributes wa;
  wa.override_redirect = True;
  wa.background_pixel = col_bg;
  wa.event_mask = ExposureMask | ButtonPressMask | FocusChangeMask;
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
  if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), col_border_norm_hex, &xftcol_dim))
    die("failed to allocate xftcol_dim");
  if (showbar)
    XMapRaised(dpy, barwin);
}

static void bar_cleanup(void) {
  if (xftdraw) {
    Visual *vis = DefaultVisual(dpy, screen);
    Colormap cmap = DefaultColormap(dpy, screen);
    XftColorFree(dpy, vis, cmap, &xftcol_fg);
    XftColorFree(dpy, vis, cmap, &xftcol_bg);
    XftColorFree(dpy, vis, cmap, &xftcol_accent);
    XftColorFree(dpy, vis, cmap, &xftcol_dim);
    XftDrawDestroy(xftdraw);
    xftdraw = NULL;
  }
  if (xftfont) {
    XftFontClose(dpy, xftfont);
    xftfont = NULL;
  }
  if (topbar)
    XDestroyWindow(dpy, barwin);
}

static void run(void) {
  XEvent ev;
  int xfd = ConnectionNumber(dpy);
  time_t last = 0;
  while (running) {
    if (!XPending(dpy)) {
      struct pollfd pfds[2] = {
        { .fd = xfd, .events = POLLIN },
        { .fd = voxtype_fd, .events = POLLIN }
      };
      nfds_t count = voxtype_fd >= 0 ? 2 : 1;
      poll(pfds, count, 200);
      if (count > 1 && (pfds[1].revents & (POLLIN | POLLHUP)))
        readvoxtypestatus();
      else if (count > 1 && (pfds[1].revents & (POLLERR | POLLNVAL)))
        stopvoxtypestatus();
    }
    while (XPending(dpy)) {
      XNextEvent(dpy, &ev);
      if (topbar && ev.xany.window == barwin && ev.type == Expose)
        drawbar();
      switch (ev.type) {
        case MapRequest: maprequest(&ev); break;
        case ButtonPress: buttonpress(&ev); break;
        case FocusIn: focusin(&ev); break;
        case ConfigureRequest: configurerequest(&ev); break;
        case ConfigureNotify: {
          XConfigureEvent *cev = &ev.xconfigure;
          if (cev->window == root && (sw != cev->width || sh != cev->height)) {
            sw = cev->width;
            sh = cev->height;
            if (topbar)
              XResizeWindow(dpy, barwin, sw, barheight);
            arrange();
          }
          break;
        }
        case ClientMessage: {
          XClientMessageEvent *cm = &ev.xclient;
          if (cm->message_type == net_wm_state) {
            Client *c = wintoclient(cm->window);
            if (c) {
              Atom states[] = { (Atom)cm->data.l[1], (Atom)cm->data.l[2] };
              int action = cm->data.l[0];
              for (unsigned int i = 0; i < LENGTH(states); i++) {
                Atom state = states[i];
                if (state == None || (i > 0 && state == states[0]))
                  continue;
                if (state != net_wm_state_fullscreen && state != net_wm_state_modal
                    && state != net_wm_state_above)
                  continue;
                int enabled = window_has_state(c->win, state);
                if (action == 0)
                  enabled = 0;
                else if (action == 1)
                  enabled = 1;
                else if (action == 2)
                  enabled = !enabled;
                else
                  continue;
                setwindowstate(c->win, state, enabled);
              }
              setfullscreen(c, window_has_state(c->win, net_wm_state_fullscreen));
              refreshclientrole(c, 0);
              arrange();
            }
          }
          break;
        }
        case DestroyNotify: destroynotify(&ev); break;
        case PropertyNotify: {
          XPropertyEvent *pev = &ev.xproperty;
          Client *c = wintoclient(pev->window);
          if (c) {
            if (pev->atom == XA_WM_CLASS)
              applyrules(c);
            if (pev->atom == net_wm_state) {
              setfullscreen(c, window_has_state(c->win, net_wm_state_fullscreen));
              refreshclientrole(c, 0);
              arrange();
            } else if (pev->atom == net_wm_window_type
                || pev->atom == XA_WM_TRANSIENT_FOR || pev->atom == XA_WM_CLASS) {
              refreshclientrole(c, 0);
              arrange();
            }
          }
          break;
        }
        case UnmapNotify: unmapnotify(&ev); break;
        case KeyPress:
        case KeyRelease: keyevent(&ev); break;
        case MappingNotify: {
          XMappingEvent *mev = &ev.xmapping;
          if (mev->request != MappingKeyboard && mev->request != MappingModifier)
            break;
          XRefreshKeyboardMapping(mev);
          grabkeys();
          if (mev->request == MappingModifier) {
            for (Client *c = clients; c; c = c->next)
              grabbuttons(c);
          }
          break;
        }
      }
    }
    reapchildren();
    time_t now = time(NULL);
    if (now - last >= status_interval) {
      last = now;
      updateclock();
    } else if (statusdirty) {
      statusdirty = 0;
      updateclock();
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
