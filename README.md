# HelloWorld (SDL2 + C++) for Fire HD 10 (Gen7) — Fire OS 5 / Android 5.1 (API 22)

A minimal SDL2 + C++ Android app — a fullscreen window with a bouncing
rectangle, touch/back-button handling, and an SDL_Log greeting — built
reproducibly from a Nix flake with `ndk-build` (no Gradle, no Android Studio).

## Layout

```
flake.nix
keystore/debug.keystore                  (fixed signing key, included)
src/AndroidManifest.xml                  (points at org.libsdl.app.SDLActivity)
src/jni/Android.mk                       (top-level ndk-build entry point)
src/jni/Application.mk                   (ABI / platform / STL settings)
src/jni/src/Android.mk                   (our "main" native module)
src/jni/src/main.cpp                     (the actual app, in C++)
```

`src/jni/SDL` is *not* checked in — it's the SDL2 2.30.3 release source,
fetched and unpacked at build time by the flake (pinned by hash, so it's
still fully reproducible). Its `android-project/.../org/libsdl/app/*.java`
files (SDLActivity and friends) are compiled in verbatim; this app has no
custom Java code at all — `AndroidManifest.xml` launches `SDLActivity`
directly, which loads `libSDL2.so` and `libmain.so` and calls our `main()`
(renamed to `SDL_main` under the hood) via JNI.

## Build

Requires Nix with flakes enabled. On first build, Nix downloads the Android
SDK platform 22/26, build-tools 30.0.3, and NDK 23.1.7779620 (all as
hash-pinned fixed-output derivations), plus the SDL2 2.30.3 source tarball.

```bash
nix build
# -> result/helloworld.apk
```

## Install

```bash
nix run .#install        # uses adb install -r
# or manually:
adb install -r result/helloworld.apk
```

Enable Developer Options on the tablet (tap "Serial Number" 7x in
Settings > Device Options), turn on ADB debugging, and allow installs from
unknown sources / ADB.

## What the app does

- Opens a fullscreen SDL2 window with a direct **OpenGL ES 3.0** context
  (`SDL_GL_CreateContext`, not `SDL_CreateRenderer` — SDL is only used for
  windowing/input/context management here, all drawing is raw GL).
- Logs `"Hello, World! from SDL2 + OpenGL ES 3.0 + C++ on Android"` via
  `SDL_Log` (visible with `adb logcat -s SDL`).
- Draws a per-vertex-colored cube (own VAO/VBO/EBO, own GLSL ES 300 vertex
  and fragment shaders, indexed `glDrawElements` draw call, depth testing
  enabled) that spins continuously around two axes while bouncing around
  the screen, with a small hand-rolled matrix library (translate/rotate/
  perspective) driving the MVP uniform — no external math dependency.
- Logs touch coordinates on finger-down.
- Exits cleanly on the Android back button or `SDL_QUIT`.

## Design notes / why things are set up this way

- **Target ABIs**: both `armeabi-v7a` and `arm64-v8a` are built. The Fire HD
  10 Gen7's MediaTek MT8173 SoC is 64-bit, but Fire OS commonly ships a
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
  actual APK still declares `minSdkVersion=21` / `targetSdkVersion=22` and
  only ever exercises the guarded, API-22-safe code paths on the Fire
  tablet.
- **Fixed signing key**: as before, `keystore/debug.keystore` is a
  committed, fixed key so the app's signature — and therefore its ability
  to reinstall over a previous build — stays stable across rebuilds.
- **`ndk-build` over CMake**: SDL2 ships both, but `ndk-build` with
  `Android.mk`/`Application.mk` is the toolchain SDL2's own Android project
  template is built around, and it composes cleanly with plain shell/Nix
  without pulling in Gradle.

## Caveats

- This flake was authored and carefully checked against SDL2's actual
  source tree and nixpkgs' `androidenv` API, but **not build-executed
  end-to-end** in the environment that produced it (no network access to
  Android's SDK/NDK distribution servers there). The SDL2 source hash *was*
  verified against a real download. If `nix build` hits a snag, the most
  likely spots are: the `ndk-bundle` symlink name (check
  `$ANDROID_HOME/ndk` if `ndk-bundle` isn't present for your nixpkgs
  revision) or minor `aapt`/`d8` flag differences across build-tools
  versions.
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
git (`git add` alone is enough, no commit required).
