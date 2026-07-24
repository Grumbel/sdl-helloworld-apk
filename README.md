# HelloWorld apps for Fire HD 10 (Gen7) — Fire OS 5 / Android 5.1 (API 22)

Two small SDL2 + C++ Android apps, built reproducibly from a single Nix
flake with `ndk-build` (no Gradle, no Android Studio):

- **`hellosdl`** — plain `SDL_Renderer`, a bouncing colored rectangle plus
  a mouse-cursor square and a colored square per active multi-touch point.
- **`hellogl`** — raw OpenGL ES (tries 3.1 → 3.0 → 2.0 at runtime and
  adapts), a spinning, bouncing, per-vertex-colored cube.

They're built from one flake because the actual complexity here — Android
SDK/NDK setup, compiling SDL2 itself and its Java glue
(`SDLActivity` & friends), the `aapt`/`zipalign`/`apksigner` pipeline,
multi-ABI packaging — is identical between them and lives in one shared
Nix build. SDL2's own compile (the expensive part: ~150 C source files ×
2 ABIs, plus its Java glue) is further factored into its own cached
derivation, `sdlAndroidLibs`, built once and reused as a **prebuilt**
library by both apps — editing just `main.cpp` in either app never
triggers a full SDL2 recompile. Only `AndroidManifest.xml`, the native
module's `Android.mk`, and `main.cpp` differ per app.

## Layout

```
flake.nix
keystore/debug.keystore              (fixed signing key, shared by both apps)
common/jni/Application.mk            (shared: ABI list, platform, STL)
common/jni/Android.mk                (shared: ndk-build entry point)
apps/hellosdl/AndroidManifest.xml
apps/hellosdl/jni/Android.mk         (native module: no GLESv3 needed)
apps/hellosdl/main.cpp               (SDL_Renderer bouncing rectangle)
apps/hellosdl/res/mipmap-*dpi/ic_launcher.png  (launcher icon, 5 densities)
apps/hellogl/AndroidManifest.xml
apps/hellogl/jni/Android.mk          (native module: links GLESv3)
apps/hellogl/main.cpp                (raw GL, spinning/bouncing cube)
apps/hellogl/res/mipmap-*dpi/ic_launcher.png   (launcher icon, 5 densities)
```

SDL2's C source and Java glue aren't checked in either — the flake fetches
the SDL2 2.30.3 release source (pinned by hash) and builds it once, as its
own `sdlAndroidLibs` derivation (`nix build .#sdlAndroidLibs` to build/
inspect it standalone). Both apps then link against its `libSDL2.so` as a
**prebuilt** library (`ndk-build`'s `PREBUILT_SHARED_LIBRARY`) instead of
recompiling SDL2's ~150 C source files themselves, and reuse its
already-compiled `classes.dex` for `SDLActivity` and friends
(`android-project/.../org/libsdl/app/*.java`, compiled in verbatim, no
custom Java code in either app) instead of re-running `javac`/`d8`. Nix
caches `sdlAndroidLibs` independently of both apps, so editing just
`main.cpp` in either one never triggers a full SDL2 rebuild.

**Both apps have distinct package/application IDs** (`com.example.hellosdl`
/ `com.example.hellogl`) so they can be installed side by side on the same
device rather than overwriting each other.

## Build

Requires Nix with flakes enabled. On first build, Nix downloads the Android
SDK platform 22/26, build-tools 30.0.3, and NDK 23.1.7779620 (all as
hash-pinned fixed-output derivations), plus the SDL2 2.30.3 source tarball,
and builds `sdlAndroidLibs` once (shared/cached across both apps below).

```bash
nix build .#hellosdl
# -> result/hellosdl-<YYYYMMDD>-<gitshortrev>.apk

nix build .#hellogl        # or just `nix build` — hellogl is the default
# -> result/hellogl-<YYYYMMDD>-<gitshortrev>.apk
```

## Install

```bash
nix run .#install-hellosdl
nix run .#install-hellogl    # or `nix run .#install` — same thing
# or manually:
adb install -r result/hellosdl-*.apk
adb install -r result/hellogl-*.apk
```

Enable Developer Options on the tablet (tap "Serial Number" 7x in
Settings > Device Options), turn on ADB debugging, and allow installs from
unknown sources / ADB.

## Desktop NixOS/Linux build (no device needed)

Both apps' `main.cpp` are plain portable SDL2/C++ (nothing Android-specific
in either one), so they also build as regular native Linux binaries — handy
for quick iteration without a tablet or emulator in the loop. The one
difference from the Android build: an `#ifdef __ANDROID__` (predefined by
the NDK toolchain, absent on desktop) picks fullscreen on Android and a
plain 800×600 window on desktop, since a fullscreen SDL window makes far
less sense for a quick `nix run` on a desktop.

```bash
nix build .#hellosdl-linux && nix run .#run-hellosdl-linux
nix build .#hellogl-linux  && nix run .#run-hellogl-linux
# or just: nix run .#run-hellosdl-linux / .#run-hellogl-linux directly
```

`hellosdl-linux` only needs SDL2 and should just work. `hellogl-linux`
additionally needs `libglvnd` for `GLES3/gl3.h` and a GLES dispatch
library — present on any NixOS desktop with OpenGL configured
(`hardware.graphics.enable = true;` — formerly `hardware.opengl.enable`).
Whether it actually gets an ES context depends on your windowing backend:
Wayland's EGL-native path tends to negotiate ES contexts more reliably
than X11's default GLX path. If `hellogl-linux` logs all three context
attempts failing, try forcing EGL on X11 first:
`SDL_VIDEO_X11_FORCE_EGL=1 nix run .#run-hellogl-linux`. Either way, the
same ES 3.1 → 3.0 → 2.0 fallback ladder described below applies here too.

## What each app does

**`hellosdl`**: opens a fullscreen `SDL_Renderer` (accelerated), logs a
greeting via `SDL_Log`, animates a bouncing rectangle. On top of that:
tracks the mouse cursor (a white square follows `SDL_MOUSEMOTION`) and
every simultaneously active touch point (`SDL_FINGERDOWN`/`FINGERMOTION`/
`FINGERUP`, up to 10 at once, each drawn as its own colored square, color
picked by touch slot so multiple fingers stay visually distinct). Touch↔
mouse event synthesis is turned off (`SDL_HINT_TOUCH_MOUSE_EVENTS` /
`SDL_HINT_MOUSE_TOUCH_EVENTS`) so the two stay independent instead of a
touch also puppeting the mouse square. Logs finger down/up events, exits
cleanly on back button / `SDL_QUIT`.

**`hellogl`**: opens a fullscreen window with a direct OpenGL ES context —
tries 3.1, then 3.0, then 2.0 at runtime, using whichever this particular
device's driver actually grants. Draws a per-vertex-colored cube (own
VBO/EBO, own GLSL vertex/fragment shaders matched to whichever context it
got, indexed `glDrawElements`, depth testing) that spins continuously
around two axes while bouncing around the screen, with a small hand-rolled
matrix library (translate/rotate/perspective) driving the MVP uniform — no
external math dependency. Logs touch coordinates and driver version info
(`GL_VERSION`/`GL_RENDERER`/`GL_VENDOR`/`GL_SHADING_LANGUAGE_VERSION`) via
`SDL_Log`. Exits cleanly on back button / `SDL_QUIT`.

## Checking what GL ES version a device actually supports

Two commands, independent of either app, tell you what a given device's
driver claims before you build/install anything:

```bash
# Android's own declared capability (what PackageManager/store filtering use)
adb shell getprop ro.opengles.version
# e.g. 131072 = 0x20000 = ES 2.0, 196608 = 0x30000 = ES 3.0, 196609 = ES 3.1

# What the compositor actually negotiated at boot (ground truth, includes
# vendor/renderer strings)
adb shell dumpsys SurfaceFlinger | grep -i GLES
```

`hellogl` also logs `GL_VERSION` / `GL_RENDERER` / `GL_VENDOR` /
`GL_SHADING_LANGUAGE_VERSION` via `SDL_Log` right after context creation
succeeds — but note that only reflects the version it *asked for and got*,
not necessarily the driver's true upper limit; `ro.opengles.version` /
SurfaceFlinger's dump are the ones to trust for that. (On the Fire HD 10
Gen7 this project was built for, the driver turned out to only genuinely
support ES 2.0, despite the GX6250 GPU's on-paper ES 3.1 capability —
`hellogl`'s runtime fallback ladder exists specifically to handle that.)

## Design notes / why things are set up this way

- **SDL2 as its own cached derivation (`sdlAndroidLibs`), linked as a
  prebuilt library**: SDL2's C compile is by far the most expensive part
  of this whole pipeline, and its output (native libs, headers, compiled
  Java glue) is 100% identical between `hellosdl` and `hellogl` — there's
  nothing app-specific in it. `sdlAndroidLibs` builds it exactly once
  (both ABIs, plus `javac`/`d8` on `SDLActivity` et al.) and exposes
  `lib/<abi>/libSDL2.so`, `include/`, and `dex/classes.dex`. Each app's
  own `mkApk` build then generates a small `Android.mk` for the `SDL2`
  module using `include $(PREBUILT_SHARED_LIBRARY)` instead of building
  it from source — a standard `ndk-build` mechanism — pointing straight
  at that already-built `.so`. `ndk-build` still copies it into the app's
  own `libs/<abi>/` as part of its normal install step (same as it
  already did for `libc++_shared.so`), so the final APK is unaffected;
  only the *compiling* is skipped. The app's own `Android.mk` files don't
  change at all for this — they still just say
  `LOCAL_SHARED_LIBRARIES := SDL2`. Net effect: `nix build .#hellogl`
  after only touching `apps/hellogl/main.cpp` recompiles just that one
  file, not all of SDL2.
- **Launcher icons**: plain legacy PNG mipmaps at the five standard density
  buckets (`mdpi` 48px, `hdpi` 72px, `xhdpi` 96px, `xxhdpi` 144px,
  `xxxhdpi` 192px), downscaled once per bucket directly from each
  source image (no upscaling — sources were already ≥323px). No adaptive
  icon (`<adaptive-icon>` XML, separate foreground/background layers) is
  used, since that's an API 26+ feature and this app targets API 22; a
  single flat PNG per density is exactly what Android 5.1 expects. This
  is also why `aapt package` now gets a `-S src/res` flag — previously
  there were no resources at all (no `res/` dir existed), so the manifest
  couldn't reference `@mipmap/ic_launcher` until resource compilation was
  wired in.
- **One shared `mkApk` Nix function, two `apps/<name>/` directories**:
  the SDK/NDK/SDL/javac/dex/packaging pipeline is ~90% of the total
  complexity and completely identical between the two apps; only
  `AndroidManifest.xml`, one native-module `Android.mk`, and `main.cpp`
  actually differ, so those are the only per-app files.
- **Runtime GL context fallback (ES 3.1 → ES 3.0 → ES 2.0) in `hellogl`**:
  this project's target Fire tablet's GL driver rejects *both* of SDL's
  EGL context-creation paths for ES3 — the simple one (used for a plain
  "3.0" request, which SDL implements via `EGL_CONTEXT_CLIENT_VERSION=3`)
  fails with `EGL_BAD_ATTRIBUTE`, and the extended one (used for anything
  else, via the `EGL_KHR_create_context` extension) isn't available on
  this driver at all. Rather than hard-require ES3 and refuse to run,
  `main.cpp` tries ES 3.1, then ES 3.0, then ES 2.0, logging why each
  attempt failed, and adapts at runtime to whichever one actually
  succeeds: it picks a matching GLSL shader source (`#version 300 es` vs.
  legacy `attribute`/`varying` ES 2.0 GLSL) and skips VAOs (an ES3-only
  feature — buffers/attrib pointers are bound once up front instead,
  which works identically on ES2 and ES3 for a single static mesh like
  this). If you know your target hardware supports ES3 cleanly, you can
  simplify this back down to a single unconditional context request.
- **Output filenames**: `<appname>-<YYYYMMDD>-<gitshortrev>.apk`. The date
  and rev come from the flake's own git metadata (`self.shortRev` /
  `self.dirtyShortRev`, `self.lastModifiedDate`) — the *commit's* date, not
  wall-clock build time — so the name stays reproducible for a given
  commit. This needs at least one commit in the repo (`git commit`, not
  just `git add`); with no commits yet, or with uncommitted changes, you'll
  get a `-dirty` suffix on the rev (or `nogit` if there's no git history at
  all) instead of a clean short hash.
- **Target ABIs**: both `armeabi-v7a` and `arm64-v8a` are built. The Fire
  HD 10 Gen7's MediaTek MT8173 SoC is 64-bit, but Fire OS commonly ships a
  32-bit userland even on 64-bit hardware — `adb shell getprop
  ro.product.cpu.abilist` often reports `armeabi-v7a,armeabi` only, in which
  case `INSTALL_FAILED_NO_MATCHING_ABIS` is what you'll see from an
  arm64-only build. Building both and letting the package manager pick
  avoids needing to know which one in advance.
- **`compilePlatform = "33"` vs `packagePlatform = "22"`**: SDL's Java glue
  (`SDLActivity`, `HIDDeviceManager`, `SDLControllerManager`, etc.)
  references symbols up to API 31 — e.g. `PointerIcon` (24),
  `Manifest.permission.BLUETOOTH_CONNECT` (31) — all correctly guarded at
  runtime behind `Build.VERSION.SDK_INT` checks. They just need a *newer
  platform jar on the javac classpath* to resolve at compile time; the
  actual APKs still declare `minSdkVersion=21` / `targetSdkVersion=22` and
  only ever exercise the guarded, API-22-safe code paths on the Fire
  tablet.
- **Fixed signing key**: `keystore/debug.keystore` is a committed, fixed
  key (shared by both apps) so their signatures — and therefore their
  ability to reinstall over previous builds — stay stable across rebuilds.
- **`ndk-build` over CMake**: SDL2 ships both, but `ndk-build` with
  `Android.mk`/`Application.mk` is the toolchain SDL2's own Android project
  template is built around, and it composes cleanly with plain shell/Nix
  without pulling in Gradle.

## Caveats

- This flake was authored and carefully checked against SDL2's actual
  source tree and nixpkgs' `androidenv` API, and the SDL2 source hash was
  verified against a real download, but individual build steps were only
  end-to-end verified through iterative debugging against the actual
  target device (see the ES3→ES2 fallback story above) — the `hellosdl`
  app's build path reuses that same verified pipeline but wasn't
  separately re-tested on-device after the refactor.
- Exact byte-for-byte APK reproducibility isn't guaranteed (legacy
  `aapt`/`zipalign` embed zip timestamps rather than honoring
  `SOURCE_DATE_EPOCH`) — app content, native code, and signing key are all
  still fully deterministic.

## Regenerating the keystore (only if you want a different key)

```bash
keytool -genkeypair -v \
  -keystore keystore/debug.keystore -storepass android -keypass android \
  -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
  -dname "CN=Android Debug,O=Android,C=US"
```

Then `git add -f keystore/debug.keystore` — flakes only see files known to
git. A plain `git add` (no commit) is enough for `nix build` itself to
work, but you'll want an actual `git commit` too if you want a clean
`gitshortrev` in the output filename rather than a `-dirty`/`nogit`
fallback (see "Output filenames" above).
