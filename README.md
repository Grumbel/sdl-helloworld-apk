# HelloWorld apps for Fire HD 10 (Gen7) — Fire OS 5 / Android 5.1 (API 22)

Two small SDL2 + C++ Android apps, built reproducibly from a single Nix
flake with `ndk-build` (no Gradle, no Android Studio):

- **`hellosdl`** — plain `SDL_Renderer`, a bouncing colored rectangle.
- **`hellogl`** — raw OpenGL ES (tries 3.1 → 3.0 → 2.0 at runtime and
  adapts), a spinning, bouncing, per-vertex-colored cube.

They're built from one flake because the actual complexity here — Android
SDK/NDK setup, fetching and vendoring SDL2's own Java glue
(`SDLActivity` & friends), the `javac`/`d8`/`aapt`/`zipalign`/`apksigner`
pipeline, multi-ABI packaging — is identical between them and lives in one
shared Nix function. Only `AndroidManifest.xml`, the native module's
`Android.mk`, and `main.cpp` differ per app.

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

`src/jni/SDL` is *not* checked in for either app — it's the SDL2 2.30.3
release source, fetched and unpacked at build time by the flake (pinned
by hash, so it's still fully reproducible, and only downloaded once —
Nix caches it and reuses the same store path for both apps' builds). Its
`android-project/.../org/libsdl/app/*.java` files (`SDLActivity` and
friends) are compiled in verbatim for both apps; neither has any custom
Java code — each `AndroidManifest.xml` launches `SDLActivity` directly.

**Both apps have distinct package/application IDs** (`com.example.hellosdl`
/ `com.example.hellogl`) so they can be installed side by side on the same
device rather than overwriting each other.

## Build

Requires Nix with flakes enabled. On first build, Nix downloads the Android
SDK platform 22/26, build-tools 30.0.3, and NDK 23.1.7779620 (all as
hash-pinned fixed-output derivations), plus the SDL2 2.30.3 source tarball
(shared between both apps).

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

## What each app does

**`hellosdl`**: opens a fullscreen `SDL_Renderer` (accelerated), logs a
greeting via `SDL_Log`, animates a bouncing rectangle, logs touch
coordinates on finger-down, exits cleanly on back button / `SDL_QUIT`.

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
