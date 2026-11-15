/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * iOS: one monitor, the device's screen. An HMONITOR is 1. The embedding application says how large the screen is and how
 * fast it refreshes (dxmt_ios_set_screen, in pixels and Hz) before a device is made; until it does, a 13-inch iPad's.
 * The modes offered are the screen itself and smaller sizes of its shape (from two fifths of it up: a tablet's screen is a great
 * many pixels for its GPU), and two 16:9 sizes, each at 60 Hz and at the screen's rate.
 */

#include "wsi_monitor.hpp"
#include <cstring>

namespace {
uint32_t g_width = 2752, g_height = 2064, g_hz = 120;
const HMONITOR kScreen = (HMONITOR)(uintptr_t)1;
} // namespace

extern "C" __attribute__((visibility("default"))) void
dxmt_ios_set_screen(uint32_t width, uint32_t height, uint32_t hz) {
  if (width && height) {
    g_width = width;
    g_height = height;
  }
  if (hz)
    g_hz = hz;
}

namespace dxmt::wsi {

HMONITOR
getDefaultMonitor() {
  return kScreen;
}

HMONITOR
enumMonitors(uint32_t index) {
  return index == 0 ? kScreen : nullptr;
}

bool
getDisplayName(HMONITOR hMonitor, WCHAR (&Name)[32]) {
  if (hMonitor != kScreen)
    return false;
  const char *name = "\\\\.\\DISPLAY1";
  size_t i = 0;
  for (; name[i] && i < 31; i++)
    Name[i] = (WCHAR)name[i];
  Name[i] = 0;
  return true;
}

bool
getDesktopCoordinates(HMONITOR hMonitor, RECT *pRect) {
  if (hMonitor != kScreen)
    return false;
  pRect->left = 0;
  pRect->top = 0;
  pRect->right = (LONG)g_width;
  pRect->bottom = (LONG)g_height;
  return true;
}

static void
fill(WsiMode *m, uint32_t w, uint32_t h, uint32_t hz) {
  m->width = w;
  m->height = h;
  m->refreshRate = WsiRational{hz, 1};
  m->bitsPerPixel = 32;
  m->interlaced = false;
}

bool
getDisplayMode(HMONITOR hMonitor, uint32_t modeNumber, WsiMode *pMode) {
  if (hMonitor != kScreen)
    return false;
  // ascending by size; the last pair is the screen itself
  const uint32_t sizes[][2] = {{1280, 720}, {1920, 1080}, {g_width * 2 / 5 & ~1u, g_height * 2 / 5 & ~1u},
                                {g_width * 9 / 20 & ~1u, g_height * 9 / 20 & ~1u}, {g_width / 2, g_height / 2},
                                {g_width * 2 / 3 & ~1u, g_height * 2 / 3 & ~1u}, {g_width * 3 / 4, g_height * 3 / 4}, {g_width, g_height}};
  const uint32_t rates[2] = {60, g_hz};
  uint32_t nrates = g_hz > 60 ? 2 : 1, nsizes = sizeof sizes / sizeof *sizes;
  if (modeNumber >= nsizes * nrates)
    return false;
  fill(pMode, sizes[modeNumber / nrates][0], sizes[modeNumber / nrates][1], rates[modeNumber % nrates]);
  return true;
}

bool
getCurrentDisplayMode(HMONITOR hMonitor, WsiMode *pMode) {
  if (hMonitor != kScreen)
    return false;
  fill(pMode, g_width, g_height, g_hz);
  return true;
}

bool
getDesktopDisplayMode(HMONITOR hMonitor, WsiMode *pMode) {
  return getCurrentDisplayMode(hMonitor, pMode);
}

} // namespace dxmt::wsi
