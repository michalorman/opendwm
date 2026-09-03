#define MODKEY Mod4Mask

/* Layout & tiling behavior */
static const unsigned int borderpx = 2;   /* window border thickness in pixels */
static int gappx = 10;                    /* inner gaps between windows (px) */
static const int maxgaps = 50;            /* max allowed gap size (px) */
static const int minmasterw = 300;        /* minimum master width (px) */
static const int minstackw = 300;         /* minimum stack width (px) */
static float mfact = 0.60f;               /* master area fraction (0.05 - 0.95) */
static const unsigned int nmaster = 1;    /* number of master windows */

/* Bar & status */
static const int topbar = 1;              /* 1 = top bar, 0 = no bar */
static const unsigned int barheight = 22; /* bar height in pixels */
static const int status_interval = 60;    /* seconds between status refresh */
static const char *fontname = "fixed";
static const char *clockfmt = "%d %b %Y  %H:%M";

/* Colors */
static const char *col_bg_hex = "#1a1b26";
static const char *col_fg_hex = "#c0caf5";
static const char *col_accent_hex = "#7aa2f7";
static const char *col_border_focus_hex = "#7aa2f7";
static const char *col_border_norm_hex = "#3b4261";

/* Spawn commands */
static const char *termcmd[] = { "xterm", NULL };
static const char *volupcmd[] = { "wpctl", "set-volume", "--limit", "1.0", "@DEFAULT_AUDIO_SINK@", "5%+", NULL };
static const char *voldowncmd[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "5%-", NULL };
static const char *volmutecmd[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle", NULL };
static const char *notesmenucmd[] = { "notes-menu", NULL };
static const char *dictationstartcmd[] = { "dictation", "start", NULL };
static const char *dictationstopcmd[] = { "dictation", "stop", NULL };
static const char *scratchtermcmd[] = { "xterm", "-name", "opendwm-scratch-terminal", NULL };

/* Scratchpads: { X11 class, X11 instance, command } */
static const Scratchpad scratchpads[] = {
  { "XTerm", "opendwm-scratch-terminal", scratchtermcmd },
};

/* Window matching rules: { class, isfloating } */
static const Rule rules[] = {
  { NULL, 0 },
};

/* Key bindings */
static const Key keys[] = {
  TAGKEYS(XK_1, 0),
  TAGKEYS(XK_2, 1),
  TAGKEYS(XK_3, 2),
  TAGKEYS(XK_4, 3),
  TAGKEYS(XK_5, 4),
  TAGKEYS(XK_6, 5),
  TAGKEYS(XK_7, 6),
  TAGKEYS(XK_8, 7),
  TAGKEYS(XK_9, 8),
  TAGKEYS(XK_0, 9),

  { MODKEY, XK_j, focusstack, { .i = +1 } },
  { MODKEY, XK_k, focusstack, { .i = -1 } },
  { MODKEY, XK_Return, spawn, { .v = termcmd } },
  { MODKEY|ShiftMask, XK_Return, promotemaster, { .i = 0 } },
  { MODKEY, XK_c, killclient, { .i = 0 } },
  { MODKEY|ShiftMask, XK_q, quit, { .i = 0 } },
  { MODKEY|ShiftMask, XK_j, movestack, { .i = +1 } },
  { MODKEY|ShiftMask, XK_k, movestack, { .i = -1 } },

  { MODKEY, XK_t, setlayout, { .i = LAYOUT_TILE } },
  { MODKEY, XK_m, setlayout, { .i = LAYOUT_MONOCLE } },

  { MODKEY, XK_b, togglebar, { .i = 0 } },
  { MODKEY, XK_n, spawn, { .v = notesmenucmd } },
  { MODKEY|ShiftMask, XK_i, togglescratchpad, { .ui = 0 } },

  { MODKEY, XK_h, incmfact, { .f = -0.05f } },
  { MODKEY, XK_l, incmfact, { .f = +0.05f } },

  { MODKEY, XK_minus, incgaps, { .i = -2 } },
  { MODKEY, XK_KP_Subtract, incgaps, { .i = -2 } },
  { MODKEY, XK_equal, incgaps, { .i = +2 } },
  { MODKEY|ShiftMask, XK_equal, incgaps, { .i = +2 } },
  { MODKEY, XK_plus, incgaps, { .i = +2 } },
  { MODKEY, XK_KP_Add, incgaps, { .i = +2 } },

  { 0, XF86XK_AudioRaiseVolume, spawnserial, { .v = volupcmd } },
  { 0, XF86XK_AudioLowerVolume, spawnserial, { .v = voldowncmd } },
  { 0, XF86XK_AudioMute, spawnserial, { .v = volmutecmd } },
  { 0, XK_F9, spawn, { .v = dictationstartcmd } },

};

/* Key release bindings */
static const Key releasekeys[] = {
  { 0, XK_F9, spawn, { .v = dictationstopcmd } },
};

/* Mouse bindings */
static const Button buttons[] = {
  { MODKEY, Button1, movemouse, { .i = 0 } },
};
