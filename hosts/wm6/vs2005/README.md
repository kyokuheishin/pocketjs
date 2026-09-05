# PocketJS Windows Mobile 6 VS2005 projects

This solution contains three native Smart Device applications for the HP iPAQ
212 port:

- `PocketJS.WM6.Probe` is the first hardware gate. It exercises the screen,
  GDI, stylus, keys, timer, memory reporting, and deployment path.
- `PocketJS.WM6.Vapor` runs the repository's Pocket Vapor Todo component after
  ahead-of-time compilation to portable C. It is the first application UI on
  WM6, but it is not a QuickJS host and cannot load ordinary PocketJS bundles.
- `PocketJS.WM6.QuickJS` builds a VC8 host and deploys the CeGCC-built QuickJS
  DLL plus the real `apps/cards` bundle (`Feature Cards`). That DLL contains
  the native ARMv4T Rust PocketJS core. QuickJS HostOps call the core's real
  tree, styles, Taffy layout, animation, texture/font, and software-raster
  APIs. Each incremental ARGB32 core frame is converted to RGB565, written to
  a lockable DirectDraw offscreen surface, and blitted to the primary surface.
  Drivers without a usable DirectDraw path fall back to a 32-bit GDI DIB
  backed by the same software framebuffer. The host requests the absolute
  `DMDO_90` orientation relative to the device's default portrait mode before
  mounting the bundle and restores the previous mode when it exits. The
  rotated `SM_CXSCREEN` and `SM_CYSCREEN` values determine the physical output.
  The host uses a fixed 480x272 logical viewport, scales it to fit while
  preserving aspect ratio, centers it with black bars, and maps stylus
  coordinates back through that transform. The window title reports whether
  landscape rotation succeeded, even without a debugger.
  Once the top-level window is foreground and sized to the whole rotated
  screen, the host resolves `SHFullScreen` from `aygshell.dll` at runtime to
  hide the taskbar, Start icon, and SIP button. SDKs whose import library omits
  that symbol use the Pocket PC `HHTaskBar` window as a fallback. The shell
  chrome is restored during normal window destruction.
  Pixel masks are queried from the primary surface separately after a display
  rotation; a missing 16-bit mask falls back to the WM6 RGB565 layout instead
  of silently converting every source color to black.
  Legacy drivers get several compatible offscreen-surface capability requests;
  the relaxed system-memory variant explicitly inherits the primary surface's
  pixel format so that it remains lockable and suitable as a `Blt` source.
  The presenter also tolerates CE drivers that omit width and height from a
  successful `Lock` descriptor and reuses the validated primary pixel format
  when the offscreen query omits it.
  If all of them fail, the 32-bit GDI fallback copies PocketJS BGRA rows
  directly instead of building both RGB565 and BGRA buffers pixel by pixel.
  While DirectDraw remains active, the GDI copy is deferred until it is
  actually needed, and matching RGB565 surfaces use a row copy instead of
  converting every 16-bit pixel a second time.
  The recurring FPS receipt also reports average QuickJS/core, framebuffer
  conversion, and presentation times so emulator and device bottlenecks can
  be distinguished without a profiler.

The Probe and Vapor applications are VC8-compatible Smart Device projects
rather than desktop Win32 projects. The QuickJS deployment anchor is also a
VC8 Smart Device project; only its QuickJS DLL comes from CeGCC. The probe
exercises the OS surface that a future full PocketJS host will need:

- ARMV4I code generation for the PXA310 device;
- a fullscreen, dynamically sized native window;
- a double-buffered GDI presentation loop;
- a `HI_RES_AWARE` executable resource so VGA devices expose native pixels;
- stylus press, drag, and release coordinates;
- hardware key events;
- runtime screen and memory reporting.

The hardware and Vapor executables do not embed QuickJS or the PocketJS
retained UI core. The QuickJS project does: its v3 DLL ABI owns the Rust core
and forwards native HostOps, PAK loading, fixed-step frames, and framebuffer
capture; see
[`docs/WM6_IPAQ_212.md`](../../../docs/WM6_IPAQ_212.md) for the staged port.

## Build

1. Install Visual Studio 2005 Standard or higher with **Visual C++ Smart
   Device Programmability**.
2. Install Visual Studio 2005 SP1 and the relevant Vista update if the build
   VM uses Vista.
3. Install **Windows Mobile 6 Professional SDK Refresh**. Microsoft maps
   Windows Mobile Classic/Pocket PC devices to this SDK; the similarly named
   Standard SDK is for non-touchscreen Smartphones.
4. Open `PocketJS.WM6.sln`.
5. To rebuild the Cards host assets under WSL, run `bun tools/build.ts cards-main`
   and `bash hosts/wm6/quickjs/build-demo.sh`. Reuse the existing
   `PocketJS.WM6.QuickJS.v3.dll`; its ABI is unchanged.
6. Select `Release | Windows Mobile 6 Professional SDK (ARMV4I)`.
7. Right-click the project you want to run and choose **Set as StartUp
   Project**.
8. Rebuild `PocketJS.WM6.QuickJS.exe` in Visual Studio and deploy it with the
   JavaScript and PAK assets. Copying only the JS/PAK is insufficient because
   the host viewport, title, and input code are native. The deployed assets are
   `PocketJS.WM6.QuickJS.v3.dll`, `PocketJS.WM6.Demo.js`, and
   `PocketJS.WM6.Demo.pak`.

The applications have no MFC, ATL, .NET Compact Framework, or redistributable
runtime dependency.

The real runtime project deploys `PocketJS.WM6.QuickJS.v3.dll`; the ABI suffix
prevents Windows CE from reusing an older QuickJS module still loaded by the
standalone Probe or a previous host process. After changing ABI versions,
close every old PocketJS process (or soft-reset the emulator) before deploying.
The primary executable is `PocketJS.WM6.QuickJS.exe`. VS2005 stores its remote
debugger target in a machine-specific ignored `.user` file, so an older
workspace may still request `PocketJS.WM6.QuickJS.Probe.exe`. The post-build
step deploys that name as a byte-for-byte compatibility alias of the current
runtime.
The VS2005 Output window reports the loaded ABI, viewport and asset sizes, the
first Rust framebuffer geometry, the actual DirectDraw surface format, and a
rolling measured FPS. Any runtime, framebuffer-copy, or DirectDraw failure
also appears in a message box instead of silently leaving a black screen.
The first frame additionally emits one-shot `trace` lines around the
JavaScript frame call, pending jobs, Rust tick, software raster, ARGB32
conversion, and DirectDraw offscreen-surface lock. It also reports the number of
non-transparent and colored Rust pixels. If startup stalls, the final trace
line identifies the exact stage without adding per-frame logging overhead.
Opening DirectDraw does not by itself mark a frame as presentable: the initial
`WM_PAINT` fills the window through GDI and waits until the first Rust frame
has been copied. Later paint requests finish and release their GDI paint DC
before presenting. Windows CE drivers that reject direct primary-surface
locking are handled through offscreen-surface Blt, with `StretchDIBits` as a
last-resort presenter.

## QuickJS Cards controls

Left and Right move focus between cards. Enter or Space presses Circle and
toggles the focused card's detail panel. A touch down on a card focuses and
activates it once; holding the stylus does not repeat. Touches starting in the
black bars are ignored.

Emulator acceptance is pending: rebuild the host, confirm the complete three-card
layout and moving background, exercise keys and stylus, and check that Escape
restores the original orientation. The startup log should report
`viewport=480x272` and `Rust frame 480x272 stride=1920 bytes=522240`.

## Pocket Vapor Todo controls

The WM6 host maps the D-pad directly. Centre/Enter is A, Back is B, and the two
soft keys are Select and Start. On an emulator without those buttons, stylus
taps provide a minimal fallback:

- top third: Up;
- middle third: Down;
- bottom-left: A;
- bottom-right: B.

The Todo component itself uses Up/Down to select, A to toggle, B to delete,
Right to change the filter, and Start to open the editor. The checked-in
`generated\todo.gba.c` is deterministic output from
`vapor\examples\todo\todo.tsx`; the WM6 host reuses its 30×20 logical grid and
RGB555 style table. `runtime\vapor.h` and `runtime\vapor_core.c` are checked-in
copies of the repository runtime so the `vs2005` directory remains
self-contained when it is mounted as `Y:\vs2005` in the build VM.

Press an unmapped Back/Escape key to exit. If the ROM consumes that key, stop
the application from **Settings > System > Memory > Running Programs**.

If the probe reports `240 x 320` on a 480×640 iPAQ, the device is running an
older executable without the `HI_RES_AWARE` resource. Clean the solution,
rebuild it, and confirm that `resources\probe.rc` appears in the build log
before redeploying.
