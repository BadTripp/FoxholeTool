/*
  Simple cross‑platform autoclicker / key holder.

  Default hotkeys:
    F2  -> spam left click at saved position (~30ms)
    F3  -> hold W
    F4  -> hold S
    F6  -> hold right mouse button
    F7  -> hold left mouse button
    F9  -> suspend/resume all actions
    F10 -> exit
*/

#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
  #define _XOPEN_SOURCE   700
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #ifndef PROCESS_QUERY_LIMITED_INFORMATION
    #define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
  #endif
#else
  #include <unistd.h>     //usleep, readlink
  #include <strings.h>    //strcasecmp
  #include <pthread.h>
  #include <sys/select.h>
  #include <X11/Xlib.h>
  #include <X11/Xutil.h>
  #include <X11/keysym.h>
  #include <X11/Xatom.h>
  #include <X11/extensions/XTest.h>
#endif

//shared global state
static atomic_int g_running = 1;
static atomic_int g_suspended = 0;

static atomic_int g_spam_left = 0;
static atomic_int g_hold_w = 0;
static atomic_int g_hold_s = 0;
static atomic_int g_hold_lmb = 0;
static atomic_int g_hold_rmb = 0;
static atomic_int g_overlay_hidden = 0;   //0 = overlay visible, 1 = hidden

//saved point for "spam click at location"
static atomic_int g_saved_x = 0;
static atomic_int g_saved_y = 0;

//--------------- hotkey logical mapping ---------------

enum {
  ACTION_SPAM_LMB = 0,
  ACTION_HOLD_W,
  ACTION_HOLD_S,
  ACTION_HOLD_RMB,
  ACTION_HOLD_LMB,
  ACTION_SUSPEND,
  ACTION_EXIT,
  ACTION_COUNT
};

static const char* g_action_names[ACTION_COUNT] = {
  "Spam LMB",
  "Hold W",
  "Hold S",
  "Hold RMB",
  "Hold LMB",
  "Suspend",
  "Exit"
};

#define CONFIG_FILE "foxtool_hotkeys.cfg"

//action keys -> platform codes (VK_* / XK_*)
static int g_action_keys[ACTION_COUNT] = {0};

//--------------- helper functions ------------------
static void msleep(int ms) {
#ifdef _WIN32
  Sleep((DWORD)ms);
#else
  if (ms <= 0) return;
  struct timespec ts;
  ts.tv_sec  = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
#endif
}

//monotonic-ish time for periodic loops
static uint64_t now_ms(void) {
#ifdef _WIN32
  return (uint64_t)GetTickCount64();
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
#endif
}

//--------------- small helpers ------------------
//case-insensitive substring search
static int strcasestr_simple(const char *haystack, const char *needle) {
  if (!haystack || !needle || !*needle) return 0;
  for (const char *p = haystack; *p; ++p) {
    const char *h = p;
    const char *n = needle;
    while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
      ++h;
      ++n;
    }
    if (!*n) return 1;
  }
  return 0;
}

//overlay size in pixels
#define OVERLAY_WIDTH_FULL    800u
#define OVERLAY_WIDTH_COMPACT 260u
#define OVERLAY_HEIGHT        32u

//convert string to uppercase A‑Z
static void strtoupper_simple(char *s) {
  if (!s) return;
  for (; *s; ++s) {
    if (*s >= 'a' && *s <= 'z') {
      *s = (char)(*s - 'a' + 'A');
    }
  }
}

//---- map key code <-> readable name (e.g. VK_F2 -> "F2") ----

#ifdef _WIN32
static const char* key_name_from_code(int vk) {
  switch (vk) {
    case VK_F2:  return "F2";
    case VK_F3:  return "F3";
    case VK_F4:  return "F4";
    case VK_F6:  return "F6";
    case VK_F7:  return "F7";
    case VK_F8:  return "F8";
    case VK_F9:  return "F9";
    case VK_F10: return "F10";
    case VK_F11: return "F11";
    default:     return "?";
  }
}

static int key_code_from_name(const char *name) {
  if (!name || !*name) return 0;
  char buf[16];
  snprintf(buf, sizeof(buf), "%s", name);
  buf[sizeof(buf)-1] = '\0';
  strtoupper_simple(buf);

  if (strcmp(buf, "F2")  == 0) return VK_F2;
  if (strcmp(buf, "F3")  == 0) return VK_F3;
  if (strcmp(buf, "F4")  == 0) return VK_F4;
  if (strcmp(buf, "F6")  == 0) return VK_F6;
  if (strcmp(buf, "F7")  == 0) return VK_F7;
  if (strcmp(buf, "F8")  == 0) return VK_F8;
  if (strcmp(buf, "F9")  == 0) return VK_F9;
  if (strcmp(buf, "F10") == 0) return VK_F10;
  if (strcmp(buf, "F11") == 0) return VK_F11;
  return 0;
}
#else
static const char* key_name_from_code(int ks) {
  switch (ks) {
    case XK_F2:  return "F2";
    case XK_F3:  return "F3";
    case XK_F4:  return "F4";
    case XK_F6:  return "F6";
    case XK_F7:  return "F7";
    case XK_F8:  return "F8";
    case XK_F9:  return "F9";
    case XK_F10: return "F10";
    case XK_F11: return "F11";
    default:     return "?";
  }
}

static int key_code_from_name(const char *name) {
  if (!name || !*name) return 0;
  char buf[16];
  snprintf(buf, sizeof(buf), "%s", name);
  buf[sizeof(buf)-1] = '\0';
  strtoupper_simple(buf);

  if (strcmp(buf, "F2")  == 0) return XK_F2;
  if (strcmp(buf, "F3")  == 0) return XK_F3;
  if (strcmp(buf, "F4")  == 0) return XK_F4;
  if (strcmp(buf, "F6")  == 0) return XK_F6;
  if (strcmp(buf, "F7")  == 0) return XK_F7;
  if (strcmp(buf, "F8")  == 0) return XK_F8;
  if (strcmp(buf, "F9")  == 0) return XK_F9;
  if (strcmp(buf, "F10") == 0) return XK_F10;
  if (strcmp(buf, "F11") == 0) return XK_F11;
  return 0;
}
#endif

//----set default hotkey mapping----

static void init_default_hotkeys(void) {
#ifdef _WIN32
  g_action_keys[ACTION_SPAM_LMB] = VK_F2;
  g_action_keys[ACTION_HOLD_W]   = VK_F3;
  g_action_keys[ACTION_HOLD_S]   = VK_F4;
  g_action_keys[ACTION_HOLD_RMB] = VK_F6;
  g_action_keys[ACTION_HOLD_LMB] = VK_F7;
  g_action_keys[ACTION_SUSPEND]  = VK_F9;
  g_action_keys[ACTION_EXIT]     = VK_F10;
#else
  g_action_keys[ACTION_SPAM_LMB] = XK_F2;
  g_action_keys[ACTION_HOLD_W]   = XK_F3;
  g_action_keys[ACTION_HOLD_S]   = XK_F4;
  g_action_keys[ACTION_HOLD_RMB] = XK_F6;
  g_action_keys[ACTION_HOLD_LMB] = XK_F7;
  g_action_keys[ACTION_SUSPEND]  = XK_F9;
  g_action_keys[ACTION_EXIT]     = XK_F10;
#endif
}

//---- load/save hotkey config from file ----

static void load_hotkey_config(void) {
  FILE *f = fopen(CONFIG_FILE, "r");
  if (!f) return;

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
      continue;

    char key[64], val[64];
    if (sscanf(line, " %63[^=]=%63s", key, val) != 2)
      continue;

    int action = -1;
    for (int i = 0; i < ACTION_COUNT; ++i) {
      if (strcmp(key, g_action_names[i]) == 0) {
        action = i;
        break;
      }
    }
    if (action < 0) continue;

    int code = key_code_from_name(val);
    if (code != 0) {
      g_action_keys[action] = code;
    }
  }

  fclose(f);
}

static void save_hotkey_config(void) {
  FILE *f = fopen(CONFIG_FILE, "w");
  if (!f) {
    fprintf(stderr, "Warning: cannot write config file '%s'\n", CONFIG_FILE);
    return;
  }

  for (int i = 0; i < ACTION_COUNT; ++i) {
    const char *name = g_action_names[i];
    const char *kname = key_name_from_code(g_action_keys[i]);
    fprintf(f, "%s=%s\n", name, kname);
  }

  fclose(f);
}

//--------------- system input and overlay handling ---------------

#ifdef _WIN32
//---------------- Windows input and overlay -----------------

//main overlay window handles on Windows
static HWND g_overlay_hwnd = NULL;
static HWND g_war_hwnd = NULL;
static HFONT g_overlay_font = NULL;

//F11 toggles overlay visibility
#define VK_HIDE_OVERLAY   VK_F11

static void win_send_key(WORD vk, int down) {
  INPUT in = {0};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vk;
  in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
  SendInput(1, &in, sizeof(in));
}

static void win_send_mouse_btn(int button, int down) {
  //button: 0=left 1=right
  INPUT in = {0};
  in.type = INPUT_MOUSE;
  if (button == 0) in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN  : MOUSEEVENTF_LEFTUP;
  else             in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
  SendInput(1, &in, sizeof(in));
}

static void win_move_mouse_abs(int x, int y) {
  //Convert to absolute (0..65535)
  int sx = GetSystemMetrics(SM_CXSCREEN);
  int sy = GetSystemMetrics(SM_CYSCREEN);
  LONG ax = (LONG)((double)x * 65535.0 / (double)(sx - 1));
  LONG ay = (LONG)((double)y * 65535.0 / (double)(sy - 1));

  INPUT in = {0};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
  in.mi.dx = ax;
  in.mi.dy = ay;
  SendInput(1, &in, sizeof(in));
}

static void win_get_cursor(int *x, int *y) {
  POINT p;
  GetCursorPos(&p);
  *x = (int)p.x;
  *y = (int)p.y;
}

//move and resize overlay to match the game window
static void overlay_reposition_win(void) {
  if (!g_overlay_hwnd || !g_war_hwnd) return;

  RECT rc;
  if (!GetWindowRect(g_war_hwnd, &rc)) return;

  int x = rc.left;
  int y = 0;
  int width  = (int)OVERLAY_WIDTH_FULL;
  int height = (int)OVERLAY_HEIGHT;

  MoveWindow(g_overlay_hwnd, x, y, width, height, TRUE);
}

#else
//---------------- Linux (X11) input and overlay ----------------
static Display *dpy = NULL;

//overlay window and target game window (Linux/X11)
static Window g_overlay_win = 0;
static Window g_foxhole_win = 0;
static XFontStruct *g_overlay_font = NULL;
static Visual *g_argb_visual = NULL;
static int g_argb_depth = 0;
static Colormap g_argb_colormap = 0;
static GC g_overlay_gc = 0;
static unsigned long g_overlay_white_pixel = 0;

//F11 toggles overlay visibility
#define KS_HIDE_OVERLAY   XK_F11

static void x11_get_cursor(int *x, int *y) {
  Window root = DefaultRootWindow(dpy);
  Window ret_root, ret_child;
  int root_x, root_y, win_x, win_y;
  unsigned int mask;
  XQueryPointer(dpy, root, &ret_root, &ret_child, &root_x, &root_y, &win_x, &win_y, &mask);
  *x = root_x;
  *y = root_y;
}

static void x11_move_mouse(int x, int y) {
  XTestFakeMotionEvent(dpy, -1, x, y, CurrentTime);
  XFlush(dpy);
}

static void x11_mouse_btn(int button, int down) {
  //XTest buttons: 1=left 3=right
  int b = (button == 0) ? 1 : 3;
  XTestFakeButtonEvent(dpy, b, down ? True : False, CurrentTime);
  XFlush(dpy);
}

static void x11_key(int keysym, int down) {
  KeyCode kc = XKeysymToKeycode(dpy, (KeySym)keysym);
  if (kc == 0) return;
  XTestFakeKeyEvent(dpy, kc, down ? True : False, CurrentTime);
  XFlush(dpy);
}
#endif

//---------- text shown in the overlay (Windows + Linux) ----------

static void build_overlay_text(char *buf, size_t buf_size) {
  if (!buf || buf_size == 0) return;

  const char *k_spam   = key_name_from_code(g_action_keys[ACTION_SPAM_LMB]);
  const char *k_w      = key_name_from_code(g_action_keys[ACTION_HOLD_W]);
  const char *k_s      = key_name_from_code(g_action_keys[ACTION_HOLD_S]);
  const char *k_rmb    = key_name_from_code(g_action_keys[ACTION_HOLD_RMB]);
  const char *k_lmb    = key_name_from_code(g_action_keys[ACTION_HOLD_LMB]);
  const char *k_suspend= key_name_from_code(g_action_keys[ACTION_SUSPEND]);
  const char *k_exit   = key_name_from_code(g_action_keys[ACTION_EXIT]);
  const char *k_hide   = key_name_from_code(
#ifdef _WIN32
      VK_HIDE_OVERLAY
#else
      KS_HIDE_OVERLAY
#endif
  );

  snprintf(buf, buf_size,
           "%s spam LMB saved position | "
           "%s hold W | %s hold S | "
           "%s hold RMB | %s hold LMB | "
           "%s stop | %s exit | "
           "%s hide HUD",
           k_spam, k_w, k_s, k_rmb, k_lmb, k_suspend, k_exit,
           k_hide);
}

#ifndef _WIN32
//--------------- overlay helpers for Linux/X11 -----------

static void overlay_draw(void);

//helpers to read and compare window titles
static int window_title_equals(Display *display, Window w, const char *exact) {
  if (!display || !exact) return 0;
  char *name = NULL;
  if (XFetchName(display, w, &name) > 0 && name) {
    int ok = (strcasecmp(name, exact) == 0);
    XFree(name);
    return ok;
  }
  return 0;
}

static int window_title_contains(Display *display, Window w, const char *substr) {
  if (!display || !substr) return 0;
  char *name = NULL;
  if (XFetchName(display, w, &name) > 0 && name) {
    int ok = strcasestr_simple(name, substr);
    XFree(name);
    return ok;
  }
  return 0;
}

static Window find_window_title_exact_rec(Display *display, Window w, const char *exact) {
  if (window_title_equals(display, w, exact))
    return w;

  Window root_ret, parent_ret;
  Window *children = NULL;
  unsigned int nchildren = 0;
  if (!XQueryTree(display, w, &root_ret, &parent_ret, &children, &nchildren))
    return 0;

  Window result = 0;
  for (unsigned int i = 0; i < nchildren && !result; ++i) {
    result = find_window_title_exact_rec(display, children[i], exact);
  }

  if (children)
    XFree(children);

  return result;
}

static Window find_window_title_contains_rec(Display *display, Window w, const char *substr) {
  if (window_title_contains(display, w, substr))
    return w;

  Window root_ret, parent_ret;
  Window *children = NULL;
  unsigned int nchildren = 0;
  if (!XQueryTree(display, w, &root_ret, &parent_ret, &children, &nchildren))
    return 0;

  Window result = 0;
  for (unsigned int i = 0; i < nchildren && !result; ++i) {
    result = find_window_title_contains_rec(display, children[i], substr);
  }

  if (children)
    XFree(children);

  return result;
}

//return 1 if process path/cmdline contains "foxhole"
static int process_matches_foxhole(unsigned long pid_ul) {
  if (pid_ul == 0) return 0;

  char pathbuf[512];
  char procfile[64];

  //check /proc/<pid>/exe symlink
  snprintf(procfile, sizeof(procfile), "/proc/%lu/exe", pid_ul);
  ssize_t len = readlink(procfile, pathbuf, sizeof(pathbuf) - 1);
  if (len > 0) {
    pathbuf[len] = '\0';
    if (strcasestr_simple(pathbuf, "foxhole")) {
      return 1;
    }
  }

  //check first argument in /proc/<pid>/cmdline
  snprintf(procfile, sizeof(procfile), "/proc/%lu/cmdline", pid_ul);
  FILE *f = fopen(procfile, "r");
  if (f) {
    size_t n = fread(pathbuf, 1, sizeof(pathbuf) - 1, f);
    fclose(f);
    if (n > 0) {
      //cmdline is NUL‑separated; first string is enough
      pathbuf[n] = '\0';
      if (strcasestr_simple(pathbuf, "foxhole")) {
        return 1;
      }
    }
  }

  return 0;
}

static Window find_target_window(Display *display) {
  if (!display) return 0;

  Atom pid_atom = XInternAtom(display, "_NET_WM_PID", True);
  if (pid_atom == None) {
    return 0;
  }

  Window root = DefaultRootWindow(display);

  //DFS over windows belonging to a process whose path/cmdline contains "foxhole"
  Window stack[1024];
  int stack_top = 0;
  stack[stack_top++] = root;

  while (stack_top > 0) {
    Window w = stack[--stack_top];

    //read PID for this window
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    if (XGetWindowProperty(display, w, pid_atom,
                           0, 1, False, XA_CARDINAL,
                           &type, &format, &nitems, &bytes_after, &prop) == Success &&
        prop != NULL && nitems >= 1 && format == 32) {
      unsigned long pid_ul = *((unsigned long*)prop);
      XFree(prop);

      if (process_matches_foxhole(pid_ul)) {
        return w;
      }
    } else if (prop) {
      XFree(prop);
    }

    //push children on the stack to keep searching
    Window root_ret, parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;
    if (XQueryTree(display, w, &root_ret, &parent_ret, &children, &nchildren)) {
      for (unsigned int i = 0; i < nchildren && stack_top < (int)(sizeof(stack) / sizeof(stack[0])); ++i) {
        stack[stack_top++] = children[i];
      }
    }
    if (children) {
      XFree(children);
    }
  }

  return 0;
}

static void overlay_position_on_window(void) {
  if (!dpy || !g_overlay_win || !g_foxhole_win) return;

  XWindowAttributes wa;
  if (!XGetWindowAttributes(dpy, g_foxhole_win, &wa)) return;
  int x = wa.x;
  int y = 0;

  unsigned int width = OVERLAY_WIDTH_FULL;
  unsigned int height = OVERLAY_HEIGHT;

  XMoveResizeWindow(dpy, g_overlay_win, x, y, width, height);
  XMapRaised(dpy, g_overlay_win);
}

static void overlay_init(void) {
  if (!dpy) return;

  int screen = DefaultScreen(dpy);
  Window root = RootWindow(dpy, screen);

  unsigned int width = OVERLAY_WIDTH_FULL;
  unsigned int height = OVERLAY_HEIGHT;

  //if we do not have an ARGB visual yet (32‑bit with alpha), try to pick one now
  if (!g_argb_visual) {
    XVisualInfo vinfo;
    if (XMatchVisualInfo(dpy, screen, 32, TrueColor, &vinfo)) {
      g_argb_visual = vinfo.visual;
      g_argb_depth = vinfo.depth;
      g_argb_colormap = XCreateColormap(dpy, root, g_argb_visual, AllocNone);
    }
  }

  XSetWindowAttributes attrs;
  unsigned long valuemask = 0;

  if (g_argb_visual && g_argb_depth > 0 && g_argb_colormap) {
    //create an ARGB window with fully transparent background
    attrs.colormap = g_argb_colormap;
    attrs.border_pixel = 0;
    attrs.background_pixel = 0;  //ARGB = 0x00000000 (full alpha 0)
    attrs.override_redirect = True;
    valuemask = CWColormap | CWBorderPixel | CWBackPixel | CWOverrideRedirect;

    g_overlay_win = XCreateWindow(
        dpy, root,
        0, 0,
        width, height,
        0,
        g_argb_depth,
        InputOutput,
        g_argb_visual,
        valuemask, &attrs);
  } else {
    //fallback: standard visual without per‑pixel alpha
    attrs.override_redirect = True;
    valuemask = CWOverrideRedirect;

    g_overlay_win = XCreateWindow(
        dpy, root,
        0, 0,
        width, height,
        0,
        CopyFromParent,
        InputOutput,
        CopyFromParent,
        valuemask, &attrs);
  }

  //no static backbuffer, so the compositor does not reuse an old screenshot
  XSetWindowBackgroundPixmap(dpy, g_overlay_win, None);

  if (g_overlay_gc) {
    XFreeGC(dpy, g_overlay_gc);
  }
  g_overlay_gc = XCreateGC(dpy, g_overlay_win, 0, NULL);

  //try to load "Renner" font; if missing, X11 uses a default font
  if (!g_overlay_font) {
    g_overlay_font = XLoadQueryFont(dpy, "Renner-12");
    if (!g_overlay_font) {
      g_overlay_font = XLoadQueryFont(dpy, "Renner");
    }
  }

  //color used for overlay text (usually white)
  if (g_argb_colormap) {
    XColor col, dummy;
    if (XAllocNamedColor(dpy, g_argb_colormap, "white", &col, &dummy)) {
      g_overlay_white_pixel = col.pixel;
    } else {
      g_overlay_white_pixel = WhitePixel(dpy, screen);
    }
  } else {
    g_overlay_white_pixel = WhitePixel(dpy, screen);
  }

  XSelectInput(dpy, g_overlay_win, ExposureMask);
}

static void overlay_draw(void) {
  if (!dpy || !g_overlay_win) return;
  if (atomic_load(&g_overlay_hidden)) return;

  //use Renner font if it was loaded
  if (g_overlay_font) {
    XSetFont(dpy, g_overlay_gc, g_overlay_font->fid);
  }

  char buf[512];
  build_overlay_text(buf, sizeof(buf));

  char active[128];
  active[0] = '\0';
  if (atomic_load(&g_spam_left)) strcat(active, " Spam");
  if (atomic_load(&g_hold_w))   strcat(active, " W");
  if (atomic_load(&g_hold_s))   strcat(active, " S");
  if (atomic_load(&g_hold_rmb)) strcat(active, " RMB");
  if (atomic_load(&g_hold_lmb)) strcat(active, " LMB");
  if (atomic_load(&g_suspended))strcat(active, " [SUSP]");

  if (active[0] != '\0') {
    strncat(buf, " | Active:", sizeof(buf) - strlen(buf) - 1);
    strncat(buf, active, sizeof(buf) - strlen(buf) - 1);
  }

  //compute text area so we can clear only that part
  XWindowAttributes wa;
  XGetWindowAttributes(dpy, g_overlay_win, &wa);
  int text_width = wa.width;
  int text_height = OVERLAY_HEIGHT;
  size_t text_len = strlen(buf);
  if (g_overlay_font && text_len > 0) {
    text_width = XTextWidth(g_overlay_font, buf, (int)text_len);
    text_height = g_overlay_font->ascent + g_overlay_font->descent;
  }
  unsigned int clear_w = (unsigned int)(text_width + 8);
  unsigned int clear_h = (unsigned int)(text_height + 4);
  if (clear_w > (unsigned int)wa.width)  clear_w = (unsigned int)wa.width;
  if (clear_h > (unsigned int)wa.height) clear_h = (unsigned int)wa.height;

  //clear only the text region to alpha 0 when using ARGB
  if (g_argb_visual && g_argb_depth > 0 && g_argb_colormap) {
    XSetForeground(dpy, g_overlay_gc, 0x00000000u);
    XFillRectangle(dpy, g_overlay_win, g_overlay_gc,
                   0, 0, clear_w, clear_h);
  }

  int screen = DefaultScreen(dpy);
  unsigned long white_pixel = g_overlay_white_pixel ? g_overlay_white_pixel
                                                    : WhitePixel(dpy, screen);

  XSetForeground(dpy, g_overlay_gc, white_pixel);
  XDrawString(dpy, g_overlay_win, g_overlay_gc, 4, 18, buf, (int)strlen(buf));
  XFlush(dpy);
}

#endif //!_WIN32

//--------------- overlay draw stub for Windows ---
#ifdef _WIN32
static void overlay_draw(void) {
  if (g_overlay_hwnd) {
    InvalidateRect(g_overlay_hwnd, NULL, TRUE);
  }
}
#endif

//--------------- worker thread -------------------
static void set_all_up(void) {
  atomic_store(&g_spam_left, 0);
  atomic_store(&g_hold_w, 0);
  atomic_store(&g_hold_s, 0);
  atomic_store(&g_hold_lmb, 0);
  atomic_store(&g_hold_rmb, 0);

#ifdef _WIN32
  //release in case they were held
  win_send_key('W', 0);
  win_send_key('S', 0);
  win_send_mouse_btn(0, 0);
  win_send_mouse_btn(1, 0);
#else
  x11_key(XK_w, 0);
  x11_key(XK_s, 0);
  x11_mouse_btn(0, 0);
  x11_mouse_btn(1, 0);
#endif
}

#ifdef _WIN32
static DWORD WINAPI worker_thread(LPVOID unused)   //background loop for actions (Windows)
#else
static void* worker_thread(void* unused)           //background loop for actions (Linux)
#endif
{
  (void)unused;

  int w_is_down = 0, s_is_down = 0, lmb_is_down = 0, rmb_is_down = 0;
  uint64_t last_click = 0;

  while (atomic_load(&g_running)) {
    if (atomic_load(&g_suspended)) {
      //ensure nothing is held
      if (w_is_down) { 
#ifdef _WIN32
        win_send_key('W', 0);
#else
        x11_key(XK_w, 0);
#endif
        w_is_down = 0;
      }
      if (s_is_down) { 
#ifdef _WIN32
        win_send_key('S', 0);
#else
        x11_key(XK_s, 0);
#endif
        s_is_down = 0;
      }
      if (lmb_is_down) { 
#ifdef _WIN32
        win_send_mouse_btn(0, 0);
#else
        x11_mouse_btn(0, 0);
#endif
        lmb_is_down = 0;
      }
      if (rmb_is_down) { 
#ifdef _WIN32
        win_send_mouse_btn(1, 0);
#else
        x11_mouse_btn(1, 0);
#endif
        rmb_is_down = 0;
      }

      msleep(20);
      continue;
    }

    //Hold W
    if (atomic_load(&g_hold_w)) {
      if (!w_is_down) {
#ifdef _WIN32
        win_send_key('W', 1);
#else
        x11_key(XK_w, 1);
#endif
        w_is_down = 1;
      }
    } else if (w_is_down) {
#ifdef _WIN32
      win_send_key('W', 0);
#else
      x11_key(XK_w, 0);
#endif
      w_is_down = 0;
    }

    //Hold S
    if (atomic_load(&g_hold_s)) {
      if (!s_is_down) {
#ifdef _WIN32
        win_send_key('S', 1);
#else
        x11_key(XK_s, 1);
#endif
        s_is_down = 1;
      }
    } else if (s_is_down) {
#ifdef _WIN32
      win_send_key('S', 0);
#else
      x11_key(XK_s, 0);
#endif
      s_is_down = 0;
    }

    //Hold LMB
    if (atomic_load(&g_hold_lmb)) {
      if (!lmb_is_down) {
#ifdef _WIN32
        win_send_mouse_btn(0, 1);
#else
        x11_mouse_btn(0, 1);
#endif
        lmb_is_down = 1;
      }
    } else if (lmb_is_down) {
#ifdef _WIN32
      win_send_mouse_btn(0, 0);
#else
      x11_mouse_btn(0, 0);
#endif
      lmb_is_down = 0;
    }

    //Hold RMB
    if (atomic_load(&g_hold_rmb)) {
      if (!rmb_is_down) {
#ifdef _WIN32
        win_send_mouse_btn(1, 1);
#else
        x11_mouse_btn(1, 1);
#endif
        rmb_is_down = 1;
      }
    } else if (rmb_is_down) {
#ifdef _WIN32
      win_send_mouse_btn(1, 0);
#else
      x11_mouse_btn(1, 0);
#endif
      rmb_is_down = 0;
    }

    //Spam left click at saved location (every 30ms by default)
    if (atomic_load(&g_spam_left)) {
      uint64_t t = now_ms();
      if (t - last_click >= 30) {
        last_click = t;

        int x = atomic_load(&g_saved_x);
        int y = atomic_load(&g_saved_y);

#ifdef _WIN32
        //move -> click -> (optional) move back not needed
        win_move_mouse_abs(x, y);
        win_send_mouse_btn(0, 1);
        win_send_mouse_btn(0, 0);
#else
        x11_move_mouse(x, y);
        x11_mouse_btn(0, 1);
        x11_mouse_btn(0, 0);
#endif
      }
    }

    msleep(1);
  }

  //make sure everything is released
  set_all_up();

#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

//--------------- hotkey handling -----------------
static void toggle_with_log(const char* name, atomic_int* flag) {
  int v = atomic_load(flag);
  v = !v;
  atomic_store(flag, v);
  printf("%s: %s\n", name, v ? "ON" : "OFF");
  fflush(stdout);

  overlay_draw();
}

static void save_cursor_pos(void) {
  int x = 0, y = 0;
#ifdef _WIN32
  win_get_cursor(&x, &y);
#else
  x11_get_cursor(&x, &y);
#endif
  atomic_store(&g_saved_x, x);
  atomic_store(&g_saved_y, y);
  printf("Saved cursor position: (%d, %d)\n", x, y);
  fflush(stdout);
}

static void handle_action(int action) {
  switch (action) {
    case ACTION_SPAM_LMB:
      if (!atomic_load(&g_suspended)) save_cursor_pos();
      toggle_with_log("Spam LMB", &g_spam_left);
      break;
    case ACTION_HOLD_W:
      toggle_with_log("Hold W", &g_hold_w);
      break;
    case ACTION_HOLD_S:
      toggle_with_log("Hold S", &g_hold_s);
      break;
    case ACTION_HOLD_RMB:
      toggle_with_log("Hold RMB", &g_hold_rmb);
      break;
    case ACTION_HOLD_LMB:
      toggle_with_log("Hold LMB", &g_hold_lmb);
      break;
    case ACTION_SUSPEND: {
      int s = atomic_load(&g_suspended);
      s = !s;
      atomic_store(&g_suspended, s);
      printf("Suspended: %s\n", s ? "YES" : "NO");
      fflush(stdout);
      overlay_draw();
    } break;
    case ACTION_EXIT:
      atomic_store(&g_running, 0);
      break;
    default:
      break;
  }
}

#ifdef _WIN32

//internal IDs for Windows global hotkeys
enum {
  HK_ID_BASE = 1  //base ID offset
};

//simple overlay window above the "War"/Foxhole game window

static LRESULT CALLBACK overlay_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);

      //use "Renner" font for overlay text if available
      if (g_overlay_font) {
        SelectObject(hdc, g_overlay_font);
      }

      RECT rc;
      GetClientRect(hwnd, &rc);

      HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
      FillRect(hdc, &rc, bg);
      DeleteObject(bg);

      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, RGB(255, 255, 255));

      char buf[512];
      build_overlay_text(buf, sizeof(buf));

      char active[128];
      active[0] = '\0';
      if (atomic_load(&g_spam_left)) strcat(active, " Spam");
      if (atomic_load(&g_hold_w))   strcat(active, " W");
      if (atomic_load(&g_hold_s))   strcat(active, " S");
      if (atomic_load(&g_hold_rmb)) strcat(active, " RMB");
      if (atomic_load(&g_hold_lmb)) strcat(active, " LMB");
      if (atomic_load(&g_suspended))strcat(active, " [SUSP]");

      if (active[0] != '\0') {
        strncat(buf, " | Active:", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, active, sizeof(buf) - strlen(buf) - 1);
      }

      DrawTextA(hdc, buf, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      EndPaint(hwnd, &ps);
      return 0;
    }
    default:
      break;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

static BOOL CALLBACK find_foxhole_window_proc(HWND hwnd, LPARAM lParam) {
  HWND *out = (HWND *)lParam;
  if (*out) return FALSE;

  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid) {
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
      char path[MAX_PATH];
      DWORD size = sizeof(path);
      BOOL ok = QueryFullProcessImageNameA(hProc, 0, path, &size);
      CloseHandle(hProc);

      if (ok && strcasestr_simple(path, "foxhole")) {
        *out = hwnd;
        return FALSE;
      }
    }
  }

  //fallback: if path has no "foxhole", try window title ("War" or containing "foxhole")
  char title[256];
  if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
    if (_stricmp(title, "War") == 0 || strcasestr_simple(title, "foxhole")) {
      *out = hwnd;
      return FALSE;
    }
  }

  return TRUE;
}

static void overlay_init_win(void) {
  HINSTANCE hInst = GetModuleHandle(NULL);

  WNDCLASSEXA wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = overlay_wnd_proc;
  wc.hInstance = hInst;
  wc.lpszClassName = "ClickerOverlayClass";
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);

  RegisterClassExA(&wc);

  //create "Renner" font if installed; GDI will fall back to something else if not
  if (!g_overlay_font) {
    g_overlay_font = CreateFontA(
        -16, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        "Renner");
  }

  g_war_hwnd = NULL;
  EnumWindows(find_foxhole_window_proc, (LPARAM)&g_war_hwnd);

  //if we do not find the game window, we do not show the overlay
  if (!g_war_hwnd) {
    return;
  }

  g_overlay_hwnd = CreateWindowExA(
      WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
      "ClickerOverlayClass",
      "",
      WS_POPUP,
      0, 0, (int)OVERLAY_WIDTH_FULL, (int)OVERLAY_HEIGHT,
      NULL, NULL, hInst, NULL);

  if (!g_overlay_hwnd) {
    return;
  }

  //make the window text‑only: black is treated as transparent, white text is visible
  SetLayeredWindowAttributes(g_overlay_hwnd, RGB(0,0,0), (BYTE)255, LWA_COLORKEY | LWA_ALPHA);

  //position and size overlay over the game window
  overlay_reposition_win();

  ShowWindow(g_overlay_hwnd, SW_SHOWNOACTIVATE);
  UpdateWindow(g_overlay_hwnd);
}

static int register_hotkeys_win(void) {
  const int keys[] = {
    VK_F2, VK_F3, VK_F4, VK_F6, VK_F7, VK_F9, VK_F10, VK_HIDE_OVERLAY
  };
  int count = (int)(sizeof(keys) / sizeof(keys[0]));
  for (int i = 0; i < count; ++i) {
    if (!RegisterHotKey(NULL, HK_ID_BASE + i, MOD_NOREPEAT, keys[i]))
      return 0;
  }
  return 1;
}

static void unregister_hotkeys_win(void) {
  const int keys[] = {
    VK_F2, VK_F3, VK_F4, VK_F6, VK_F7, VK_F9, VK_F10, VK_HIDE_OVERLAY
  };
  int count = (int)(sizeof(keys) / sizeof(keys[0]));
  for (int i = 0; i < count; ++i) {
    UnregisterHotKey(NULL, HK_ID_BASE + i);
  }
}

#else

//helpers to register global hotkeys on Linux/X11
static void grab_key(Display* d, int keysym) {
  Window root = DefaultRootWindow(d);
  KeyCode kc = XKeysymToKeycode(d, (KeySym)keysym);
  if (kc == 0) return;

  //grab with common modifier combinations (NumLock/CapsLock)
  unsigned int mods[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
  for (size_t i = 0; i < sizeof(mods)/sizeof(mods[0]); i++) {
    XGrabKey(d, kc, mods[i], root, True, GrabModeAsync, GrabModeAsync);
  }
}

static void ungrab_key(Display* d, int keysym) {
  Window root = DefaultRootWindow(d);
  KeyCode kc = XKeysymToKeycode(d, (KeySym)keysym);
  if (kc == 0) return;

  unsigned int mods[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
  for (size_t i = 0; i < sizeof(mods)/sizeof(mods[0]); i++) {
    XUngrabKey(d, kc, mods[i], root);
  }
}

static int register_hotkeys_x11(void) {
  if (!dpy) return 0;
  grab_key(dpy, XK_F2);
  grab_key(dpy, XK_F3);
  grab_key(dpy, XK_F4);
  grab_key(dpy, XK_F6);
  grab_key(dpy, XK_F7);
//extra key: hide overlay
  grab_key(dpy, KS_HIDE_OVERLAY);
  grab_key(dpy, XK_F9);
  grab_key(dpy, XK_F10);

  XSelectInput(dpy, DefaultRootWindow(dpy), KeyPressMask);
  XFlush(dpy);
  return 1;
}

static void unregister_hotkeys_x11(void) {
  if (!dpy) return;
  ungrab_key(dpy, XK_F2);
  ungrab_key(dpy, XK_F3);
  ungrab_key(dpy, XK_F4);
  ungrab_key(dpy, XK_F6);
  ungrab_key(dpy, XK_F7);
  ungrab_key(dpy, KS_HIDE_OVERLAY);
  ungrab_key(dpy, XK_F9);
  ungrab_key(dpy, XK_F10);
  XFlush(dpy);
}

#endif

//---------------------- main ---------------------
int main(void) {
  //init and load the hotkey settings
  init_default_hotkeys();
  load_hotkey_config();

  printf("Cross-platform AutoClicker (C)\n");
  char help[512];
  build_overlay_text(help, sizeof(help));
  printf("%s\n", help);
  printf("(F11: hide/show overlay)\n");
  fflush(stdout);

#ifndef _WIN32
  //init Xlib in thread-safe mode (needed for redraw loop)
  XInitThreads();
  dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fprintf(stderr, "Error: cannot open X display. Are you on X11/Xorg?\n");
    return 1;
  }
#endif

#ifdef _WIN32
  if (!register_hotkeys_win()) {
    fprintf(stderr, "Error: failed to register hotkeys (maybe already in use?).\n");
    return 1;
  }
  HANDLE th = CreateThread(NULL, 0, worker_thread, NULL, 0, NULL);
  if (!th) {
    fprintf(stderr, "Error: failed to create worker thread.\n");
    unregister_hotkeys_win();
    return 1;
  }

  overlay_init_win();

  MSG msg;
  while (atomic_load(&g_running) && GetMessage(&msg, NULL, 0, 0)) {
    if (msg.message == WM_HOTKEY) {
      UINT vk = HIWORD(msg.lParam);

      //F11 toggles overlay visibility
      if (vk == VK_HIDE_OVERLAY) {
        int hidden = atomic_load(&g_overlay_hidden);
        hidden = !hidden;
        atomic_store(&g_overlay_hidden, hidden);
        printf("Overlay: %s\n", hidden ? "hidden" : "shown");
        fflush(stdout);
        if (g_overlay_hwnd) {
          ShowWindow(g_overlay_hwnd, hidden ? SW_HIDE : SW_SHOWNOACTIVATE);
        }
        continue;
      }

      //find action bound to this virtual-key
      int action = -1;
      for (int i = 0; i < ACTION_COUNT; ++i) {
        if (g_action_keys[i] == (int)vk) {
          action = i;
          break;
        }
      }
      if (action < 0) {
        //key not handled
        continue;
      }

      //normal mode: handle action
      handle_action(action);
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  atomic_store(&g_running, 0);
  WaitForSingleObject(th, INFINITE);
  CloseHandle(th);
  unregister_hotkeys_win();

#else
  if (!register_hotkeys_x11()) {
    fprintf(stderr, "Error: failed to register X11 hotkeys.\n");
    XCloseDisplay(dpy);
    return 1;
  }

  //Setup overlay over the War/Foxhole window (if found)
  g_foxhole_win = find_target_window(dpy);

  overlay_init();
  if (g_foxhole_win) {
    overlay_position_on_window();
  }

  pthread_t th;
  if (pthread_create(&th, NULL, worker_thread, NULL) != 0) {
    fprintf(stderr, "Error: failed to create worker thread.\n");
    unregister_hotkeys_x11();
    XCloseDisplay(dpy);
    return 1;
  }

  int xfd = ConnectionNumber(dpy);

  while (atomic_load(&g_running)) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(xfd, &fds);
    struct timeval tv;
    //faster redraw when overlay is visible or actions are active
    int wants_fast =
        !atomic_load(&g_overlay_hidden) ||
        atomic_load(&g_spam_left) ||
        atomic_load(&g_hold_w) ||
        atomic_load(&g_hold_s) ||
        atomic_load(&g_hold_lmb) ||
        atomic_load(&g_hold_rmb);
    tv.tv_sec = 0;
    tv.tv_usec = wants_fast ? 33000 : 100000; //~30ms or 100ms

    int ret = select(xfd + 1, &fds, NULL, NULL, &tv);
    if (ret > 0 && FD_ISSET(xfd, &fds)) {
      while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == Expose && ev.xexpose.window == g_overlay_win) {
          overlay_draw();
        } else if (ev.type == KeyPress) {
          KeySym ks = XLookupKeysym(&ev.xkey, 0);
          //F11 toggles overlay visibility
          if (ks == KS_HIDE_OVERLAY) {
            int hidden = atomic_load(&g_overlay_hidden);
            hidden = !hidden;
            atomic_store(&g_overlay_hidden, hidden);
            printf("Overlay: %s\n", hidden ? "hidden" : "shown");
            fflush(stdout);
            if (g_overlay_win) {
              if (hidden) {
                XUnmapWindow(dpy, g_overlay_win);
              } else {
                if (g_foxhole_win) {
                  overlay_position_on_window();
                }
                XMapRaised(dpy, g_overlay_win);
              }
              XFlush(dpy);
            }
            continue;
          }

          //find action bound to this KeySym
          int action = -1;
          for (int i = 0;  i < ACTION_COUNT; ++i) {
            if (g_action_keys[i] == (int)ks) {
              action = i;
              break;
            }
          }
          if (action < 0) {
            continue;
          }

          handle_action(action);
        }
      }
    } else if (ret == 0) {
      //timeout: periodic redraw if overlay is visible
      overlay_draw();
    }
  }

  atomic_store(&g_running, 0);
  pthread_join(th, NULL);
  unregister_hotkeys_x11();
  XCloseDisplay(dpy);
#endif

  printf("Bye.\n");
  return 0;
}
