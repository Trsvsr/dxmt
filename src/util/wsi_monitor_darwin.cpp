/*
 * Native macOS monitors for DXMT's DXGI: the displays and their modes from CoreGraphics. The headless implementation (wsi_monitor_headless.cpp) reports no monitor at
 * all, and an application that insists on an output (IDXGIAdapter::EnumOutputs) then finds no usable adapter.
 *
 * An HMONITOR is the CGDirectDisplayID. Modes are in pixels (CGDisplayModeGetPixelWidth), one per distinct size and refresh rate, ascending: a HiDPI mode and its
 * non-HiDPI twin are the same to a Direct3D application. A built-in panel reports a refresh rate of 0: that is 60 Hz here.
 */
// CoreGraphics first: the Windows shim defines `interface` as a macro, which an IOKit header it includes uses as a field name
#include <CoreGraphics/CoreGraphics.h>

#include "wsi_monitor.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace dxmt::wsi {

namespace {

std::vector<CGDirectDisplayID> displays() {
  CGDirectDisplayID ids[16];
  uint32_t n = 0;
  if (CGGetActiveDisplayList(16, ids, &n) != kCGErrorSuccess)
    n = 0;
  if (n == 0 && CGGetOnlineDisplayList(16, ids, &n) != kCGErrorSuccess)   // displays asleep (screen off, locked): online but not active; a headless run must not depend on that
    n = 0;
  std::vector<CGDirectDisplayID> v(ids, ids + n);
  CGDirectDisplayID main = CGMainDisplayID();
  std::stable_partition(v.begin(), v.end(), [&](CGDirectDisplayID d) { return d == main; });   // the main display is DISPLAY1
  return v;
}

CGDirectDisplayID id_of(HMONITOR h) {
  return h ? (CGDirectDisplayID)(uintptr_t)h : CGMainDisplayID();
}

WsiRational rational_of(double hz) {
  if (hz <= 0.0)
    return {60, 1};
  uint32_t whole = (uint32_t)(hz + 0.5);
  if (hz - (double)whole < 0.01 && (double)whole - hz < 0.01)
    return {whole, 1};
  return {(uint32_t)(hz * 1000.0 + 0.5), 1000};
}

WsiMode mode_of(CGDisplayModeRef m) {
  WsiMode w = {};
  w.width = (uint32_t)CGDisplayModeGetPixelWidth(m);
  w.height = (uint32_t)CGDisplayModeGetPixelHeight(m);
  w.refreshRate = rational_of(CGDisplayModeGetRefreshRate(m));
  w.bitsPerPixel = 32;
  w.interlaced = false;
  return w;
}

bool same(const WsiMode &a, const WsiMode &b) {
  return a.width == b.width && a.height == b.height && a.refreshRate.numerator * b.refreshRate.denominator == b.refreshRate.numerator * a.refreshRate.denominator;
}

/* The embedding application may decide which modes a Direct3D application sees (it knows its window and what it can letterbox): it exports
 * `unsigned dxmt_host_modes(const uint32_t (**modes)[3], unsigned *desktop)`, (width, height, Hz) triples in ascending order and the index of the desktop's. Looked
 * up when first needed; absent, the display's own modes (below) are used. A display can have hundreds of modes, which a fixed-size mode table in a game may refuse. */
typedef unsigned (*HostModes)(const uint32_t (**)[3], unsigned *);

HostModes host_modes() {
  static HostModes h = (HostModes)dlsym(RTLD_DEFAULT, "dxmt_host_modes");
  return h;
}

bool host_list(std::vector<WsiMode> &out, unsigned *desktop) {
  HostModes h = host_modes();
  if (!h)
    return false;
  const uint32_t (*m)[3] = nullptr;
  unsigned dt = 0, n = h(&m, &dt);
  if (!n || !m)
    return false;
  for (unsigned i = 0; i < n; i++)
    out.push_back({ m[i][0], m[i][1], { m[i][2], 1 }, 32, false });
  if (desktop)
    *desktop = dt < n ? dt : 0;
  return true;
}

std::vector<WsiMode> mode_list(CGDirectDisplayID d) {
  std::vector<WsiMode> out;
  if (d == CGMainDisplayID() && host_list(out, nullptr))
    return out;
  CFArrayRef all = CGDisplayCopyAllDisplayModes(d, nullptr);
  if (all) {
    for (CFIndex i = 0; i < CFArrayGetCount(all); i++) {
      CGDisplayModeRef m = (CGDisplayModeRef)CFArrayGetValueAtIndex(all, i);
      if (!CGDisplayModeIsUsableForDesktopGUI(m))
        continue;
      WsiMode w = mode_of(m);
      if (std::none_of(out.begin(), out.end(), [&](const WsiMode &o) { return same(o, w); }))
        out.push_back(w);
    }
    CFRelease(all);
  }
  std::sort(out.begin(), out.end(), [](const WsiMode &a, const WsiMode &b) {
    if (a.width != b.width) return a.width < b.width;
    if (a.height != b.height) return a.height < b.height;
    return a.refreshRate.numerator * b.refreshRate.denominator < b.refreshRate.numerator * a.refreshRate.denominator;
  });
  return out;
}

bool current_mode(CGDirectDisplayID d, WsiMode *pMode) {
  std::vector<WsiMode> hl;
  unsigned desktop = 0;
  if (d == CGMainDisplayID() && host_list(hl, &desktop)) {   // the embedding application speaks for the main display only
    *pMode = hl[desktop];
    return true;
  }
  CGDisplayModeRef m = CGDisplayCopyDisplayMode(d);
  if (!m)
    return false;
  *pMode = mode_of(m);
  CGDisplayModeRelease(m);
  return true;
}

} // namespace

HMONITOR getDefaultMonitor() {
  return (HMONITOR)(uintptr_t)CGMainDisplayID();
}

HMONITOR enumMonitors(uint32_t index) {
  auto d = displays();
  return index < d.size() ? (HMONITOR)(uintptr_t)d[index] : nullptr;
}

bool getDisplayName(HMONITOR hMonitor, WCHAR (&Name)[32]) {
  auto d = displays();
  CGDirectDisplayID id = id_of(hMonitor);
  size_t n = 0;
  while (n < d.size() && d[n] != id)
    n++;
  char text[32];
  snprintf(text, sizeof text, "\\\\.\\DISPLAY%zu", n + 1);
  // a Windows WCHAR[] is UTF-16: the units are packed from the start of the array, as the adapter description is
  char16_t *u = reinterpret_cast<char16_t *>(Name);
  size_t i = 0;
  for (; text[i] && i < 31; i++)
    u[i] = (char16_t)text[i];
  for (; i < 32 * sizeof(WCHAR) / sizeof(char16_t); i++)
    u[i] = 0;
  return true;
}

/* The Windows desktop is in pixels and its monitors do not overlap; CoreGraphics places displays in points, and a display's size in pixels (what a Direct3D
 * application is told) is larger than its size in points on a HiDPI display. The main display is at the origin with its mode's size. Every other display keeps
 * its own size and the side of the main display it is on: it touches that edge, and along the edge its offset is scaled as the main display is. */
bool getDesktopCoordinates(HMONITOR hMonitor, RECT *pRect) {
  CGDirectDisplayID id = id_of(hMonitor), main = CGMainDisplayID();
  CGRect b = CGDisplayBounds(id), mb = CGDisplayBounds(main);
  WsiMode m = {}, mm = {};
  if (!current_mode(id, &m)) {
    m.width = (uint32_t)b.size.width;
    m.height = (uint32_t)b.size.height;
  }
  if (!current_mode(main, &mm)) {
    mm.width = (uint32_t)mb.size.width;
    mm.height = (uint32_t)mb.size.height;
  }
  LONG left = 0, top = 0;
  if (id != main) {
    double sx = mb.size.width > 0 ? mm.width / mb.size.width : 1.0, sy = mb.size.height > 0 ? mm.height / mb.size.height : 1.0;
    if (b.origin.y + b.size.height <= mb.origin.y) {            // above
      top = -(LONG)m.height;
      left = (LONG)(b.origin.x * sx);
    } else if (b.origin.y >= mb.origin.y + mb.size.height) {    // below
      top = (LONG)mm.height;
      left = (LONG)(b.origin.x * sx);
    } else if (b.origin.x + b.size.width <= mb.origin.x) {      // to the left
      left = -(LONG)m.width;
      top = (LONG)(b.origin.y * sy);
    } else {                                                    // to the right (or mirrored: beside it)
      left = (LONG)mm.width;
      top = b.origin.x >= mb.origin.x + mb.size.width ? (LONG)(b.origin.y * sy) : 0;
    }
  }
  pRect->left = left;
  pRect->top = top;
  pRect->right = left + (LONG)m.width;
  pRect->bottom = top + (LONG)m.height;
  return true;
}

bool getDisplayMode(HMONITOR hMonitor, uint32_t modeNumber, WsiMode *pMode) {
  auto list = mode_list(id_of(hMonitor));
  if (modeNumber >= list.size())
    return false;
  *pMode = list[modeNumber];
  return true;
}

bool getCurrentDisplayMode(HMONITOR hMonitor, WsiMode *pMode) {
  return current_mode(id_of(hMonitor), pMode);
}

bool getDesktopDisplayMode(HMONITOR hMonitor, WsiMode *pMode) {
  return current_mode(id_of(hMonitor), pMode);
}

} // namespace dxmt::wsi
