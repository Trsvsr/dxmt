/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */

#include <dlfcn.h>

#include "wsi_window.hpp"
#include "wsi_monitor.hpp"

#include "util_string.hpp"
#include "log/log.hpp"

namespace dxmt::wsi {

/* The embedding application knows its window: it may export `bool dxmt_host_window_size(void *hwnd, uint32_t *w, uint32_t *h)` (looked up when first
 * needed; absent, the old 1024x1024 stands). */
typedef bool (*HostWindowSize)(void *, uint32_t *, uint32_t *);

void getWindowSize(HWND hWindow, uint32_t *pWidth, uint32_t *pHeight) {
  static HostWindowSize host = (HostWindowSize)dlsym(RTLD_DEFAULT, "dxmt_host_window_size");
  uint32_t w = 1024, h = 1024;
  if (host)
    host((void *)hWindow, &w, &h);
  if (pWidth)
    *pWidth = w;

  if (pHeight)
    *pHeight = h;
}

/* ... and `bool dxmt_host_set_fullscreen(void *hwnd, int on)` / `void dxmt_host_resize_window(void *hwnd, uint32_t w, uint32_t h)`: the game's fullscreen requests and its
 * ResizeTarget, which the host's window carries out (without them, entering fullscreen fails). */
typedef bool (*HostSetFullscreen)(void *, int);
typedef void (*HostResizeWindow)(void *, uint32_t, uint32_t);

void resizeWindow(HWND hWindow, DXMTWindowState *pState, uint32_t width,
                  uint32_t height) {
  static HostResizeWindow host = (HostResizeWindow)dlsym(RTLD_DEFAULT, "dxmt_host_resize_window");
  if (host)
    host((void *)hWindow, width, height);
}

bool setWindowMode(HMONITOR hMonitor, HWND hWindow, const WsiMode &mode) {
  return false;
}

bool enterFullscreenMode(HMONITOR hMonitor, HWND hWindow,
                         DXMTWindowState *pState,
                         [[maybe_unused]] bool modeSwitch) {
  static HostSetFullscreen host = (HostSetFullscreen)dlsym(RTLD_DEFAULT, "dxmt_host_set_fullscreen");
  return host && host((void *)hWindow, 1);
}

bool leaveFullscreenMode(HWND hWindow, DXMTWindowState *pState,
                         bool restoreCoordinates) {
  static HostSetFullscreen host = (HostSetFullscreen)dlsym(RTLD_DEFAULT, "dxmt_host_set_fullscreen");
  return host && host((void *)hWindow, 0);
}

bool restoreDisplayMode(HMONITOR hMonitor) {
  return false;
}

HMONITOR getWindowMonitor(HWND hWindow) {
  return getDefaultMonitor();            // one window system, the main display
}

bool isWindow(HWND hWindow) { return true; }

void updateFullscreenWindow(HMONITOR hMonitor, HWND hWindow,
                            bool forceTopmost) {
}

bool isForeground(HWND hWindow) { return true; }

bool isMinimized(HWND hWindow) { return false; }

} // namespace dxmt::wsi
