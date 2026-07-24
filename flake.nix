{
  description = "Reproducible SDL2 + C++ HelloWorld APKs for Fire HD 10 Gen7 (Fire OS 5 / Android 5.1, API 22): a plain SDL_Renderer app and an OpenGL ES app, sharing a single Nix build pipeline and a prebuilt SDL2 native/Java layer";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfree = true;
        config.android_sdk.accept_license = true;
      };

      buildToolsVersion = "30.0.3";
      packagePlatform = "22"; # matches Fire OS 5 / Android 5.1 exactly
      compilePlatform = "33"; # SDL's Java glue (SDLActivity, HIDDeviceManager, etc.)
                               # references symbols up to API 31 (e.g. BLUETOOTH_CONNECT)
                               # behind SDK_INT/permission guards; only needed at javac time
      ndkVersion = "23.1.7779620";
      targetAbis = [ "armeabi-v7a" "arm64-v8a" ]; # Fire tablets often run a 32-bit
                                                   # userland even on 64-bit SoCs like
                                                   # the Gen7's MT8173, so build both
                                                   # and let the installer pick.

      sdlVersion = "2.30.3";
      sdlSrc = pkgs.fetchurl {
        url = "https://github.com/libsdl-org/SDL/releases/download/release-${sdlVersion}/SDL2-${sdlVersion}.tar.gz";
        sha256 = "sha256-ggRABy+PW1AYjB2uEE8q0lmE3iaHhb5AxBoJmlEPCuw=";
      };

      androidComposition = pkgs.androidenv.composeAndroidPackages {
        platformVersions = [ packagePlatform compilePlatform ];
        buildToolsVersions = [ buildToolsVersion ];
        includeNDK = true;
        ndkVersion = ndkVersion;
        includeEmulator = false;
        includeSources = false;
      };
      androidSdk = androidComposition.androidsdk;

      # Git metadata for the output filename, derived from the flake's own
      # source (not wall-clock build time, so the name stays reproducible
      # for a given commit). Needs at least one commit in the repo — see
      # README. Falls back gracefully for an uncommitted/dirty tree.
      gitRev =
        if self ? shortRev then self.shortRev
        else if self ? dirtyShortRev then self.dirtyShortRev
        else "nogit";
      gitDate =
        if self ? lastModifiedDate then builtins.substring 0 8 self.lastModifiedDate
        else "00000000";

      # ---------------------------------------------------------------
      # SDL2 itself, built exactly once (per Nix store, cached across both
      # apps and across repeated app rebuilds): the native libSDL2.so per
      # ABI, its headers, and the compiled SDLActivity Java glue
      # (identical for every SDL2-based app — nothing app-specific here).
      # This is the expensive part of the whole pipeline (~150 C source
      # files x 2 ABIs), so pulling it out of mkApk means editing a
      # single app's main.cpp never triggers a full SDL2 recompile again.
      # ---------------------------------------------------------------
      sdlAndroidLibs = pkgs.stdenvNoCC.mkDerivation {
        pname = "sdl2-android-libs";
        version = sdlVersion;

        dontUnpack = true;
        nativeBuildInputs = [ androidSdk pkgs.jdk17 pkgs.gnumake ];

        buildPhase = ''
          runHook preBuild

          export ANDROID_HOME=${androidSdk}/libexec/android-sdk
          NDK=$ANDROID_HOME/ndk-bundle
          BT=$ANDROID_HOME/build-tools/${buildToolsVersion}
          COMPILE_JAR=$ANDROID_HOME/platforms/android-${compilePlatform}/android.jar

          mkdir -p work
          tar xzf ${sdlSrc} -C work
          mkdir -p sdl-jni
          mv work/SDL2-${sdlVersion} sdl-jni/SDL
          cp ${./common/jni/Application.mk} sdl-jni/Application.mk
          cp ${./common/jni/Android.mk} sdl-jni/Android.mk
          chmod -R u+w sdl-jni

          # Builds just SDL2 itself (module "SDL2") — no app module present,
          # so there's nothing here for an app's own main.cpp to interfere
          # with or trigger a rebuild of.
          $NDK/ndk-build \
            NDK_PROJECT_PATH=$PWD/sdl-jni \
            APP_BUILD_SCRIPT=$PWD/sdl-jni/Android.mk \
            NDK_APPLICATION_MK=$PWD/sdl-jni/Application.mk \
            -j"$NIX_BUILD_CORES"

          mkdir -p javasrc
          cp -r sdl-jni/SDL/android-project/app/src/main/java/org javasrc/org
          chmod -R u+w javasrc

          mkdir -p classes
          javac -encoding UTF-8 --release 8 -classpath "$COMPILE_JAR" -d classes \
            $(find javasrc -name '*.java')
          $BT/d8 --output classes --min-api ${packagePlatform} $(find classes -name '*.class')

          runHook postBuild
        '';

        installPhase = ''
          mkdir -p $out/lib $out/dex
          for abi in ${pkgs.lib.concatStringsSep " " targetAbis}; do
            mkdir -p $out/lib/$abi
            cp sdl-jni/libs/$abi/*.so $out/lib/$abi/
          done
          cp -r sdl-jni/SDL/include $out/include
          cp classes/classes.dex $out/dex/classes.dex
        '';
      };

      # A drop-in replacement for the real SDL2 source dir, as far as an
      # app's own jni/Android.mk is concerned: same "SDL2" module name, same
      # ../SDL/include path — just backed by sdlAndroidLibs' prebuilt .so
      # instead of full source, via ndk-build's PREBUILT_SHARED_LIBRARY.
      # No changes needed in either app's own jni/Android.mk for this.
      sdlPrebuiltAndroidMk = pkgs.writeTextFile {
        name = "SDL2-prebuilt-Android.mk";
        text = ''
          LOCAL_PATH := $(call my-dir)
          include $(CLEAR_VARS)
          LOCAL_MODULE := SDL2
          LOCAL_SRC_FILES := ${sdlAndroidLibs}/lib/$(TARGET_ARCH_ABI)/libSDL2.so
          include $(PREBUILT_SHARED_LIBRARY)
        '';
      };

      # ---------------------------------------------------------------
      # Shared build pipeline for any SDL2-based app in ./apps/<name>/:
      #   AndroidManifest.xml, jni/Android.mk (native module), main.cpp
      # Everything else (SDK/NDK setup, linking the prebuilt SDL2 layer,
      # aapt/zipalign/apksigner, ABI packaging) is identical across apps
      # and lives here once.
      # ---------------------------------------------------------------
      mkApk = { appName, appDir }:
        pkgs.stdenvNoCC.mkDerivation {
          pname = appName;
          version = "1.0.0";

          dontUnpack = true;
          nativeBuildInputs = [ androidSdk pkgs.jdk17 pkgs.zip pkgs.gnumake ];

          buildPhase = ''
            runHook preBuild

            export ANDROID_HOME=${androidSdk}/libexec/android-sdk
            NDK=$ANDROID_HOME/ndk-bundle
            BT=$ANDROID_HOME/build-tools/${buildToolsVersion}
            PACKAGE_JAR=$ANDROID_HOME/platforms/android-${packagePlatform}/android.jar

            mkdir -p src/jni/src src/jni/SDL
            cp ${./common/jni/Application.mk} src/jni/Application.mk
            cp ${./common/jni/Android.mk} src/jni/Android.mk
            cp ${appDir}/jni/Android.mk src/jni/src/Android.mk
            cp ${appDir}/main.cpp src/jni/src/main.cpp
            cp ${appDir}/AndroidManifest.xml src/AndroidManifest.xml
            cp -r ${appDir}/res src/res
            cp ${sdlPrebuiltAndroidMk} src/jni/SDL/Android.mk
            cp -r ${sdlAndroidLibs}/include src/jni/SDL/include
            chmod -R u+w src

            cp ${./keystore/debug.keystore} debug.keystore

            # --- native build: only the app's own main.cpp actually
            # compiles here; SDL2 is linked as a prebuilt .so (see
            # sdlAndroidLibs above) and just gets copied into libs/<abi>/
            # by ndk-build's install step, same as libc++_shared.so ---
            $NDK/ndk-build \
              NDK_PROJECT_PATH=$PWD/src \
              APP_BUILD_SCRIPT=$PWD/src/jni/Android.mk \
              NDK_APPLICATION_MK=$PWD/src/jni/Application.mk \
              -j"$NIX_BUILD_CORES"

            # --- package resources + manifest ---
            mkdir -p out
            $BT/aapt package -f \
              -M src/AndroidManifest.xml \
              -S src/res \
              -I "$PACKAGE_JAR" \
              -F out/base.apk

            # --- add dex (the SDLActivity glue, prebuilt by sdlAndroidLibs
            # and identical for every app — no javac/d8 needed here) and
            # native libraries ---
            cp ${sdlAndroidLibs}/dex/classes.dex out/classes.dex
            for abi in ${pkgs.lib.concatStringsSep " " targetAbis}; do
              mkdir -p out/lib/$abi
              cp src/libs/$abi/*.so out/lib/$abi/
            done

            ( cd out && $BT/aapt add base.apk classes.dex )
            ( cd out && zip -r base.apk lib )

            $BT/zipalign -f 4 out/base.apk out/aligned.apk

            $BT/apksigner sign \
              --ks debug.keystore --ks-pass pass:android --key-pass pass:android \
              --out out/${appName}.apk out/aligned.apk

            $BT/aapt dump badging out/${appName}.apk

            runHook postBuild
          '';

          installPhase = ''
            mkdir -p $out
            cp out/${appName}.apk $out/${appName}-${gitDate}-${gitRev}.apk
          '';
        };

      mkInstallApp = pkg: appName: {
        type = "app";
        program = toString (pkgs.writeShellScript "adb-install-${appName}" ''
          exec ${pkgs.android-tools}/bin/adb install -r ${pkg}/${appName}-${gitDate}-${gitRev}.apk
        '');
      };

      # ---------------------------------------------------------------
      # Desktop NixOS/Linux build of the same main.cpp files (unmodified —
      # nothing in either app is Android-specific), for quick iteration
      # without a device/emulator in the loop. hellosdl only needs SDL2;
      # hellogl additionally needs GLES3 headers + a dispatch library,
      # which libglvnd provides on desktop (note: the lib there is named
      # libGLESv2.so — desktop libglvnd has no separate libGLESv3.so the
      # way the Android NDK does, even though it exports GLES3 entry
      # points too).
      # ---------------------------------------------------------------
      mkDesktopApp = { appName, appDir, extraBuildInputs ? [], extraLdFlags ? [] }:
        pkgs.stdenv.mkDerivation {
          pname = "${appName}-linux";
          version = "1.0.0";

          dontUnpack = true;
          nativeBuildInputs = [ pkgs.pkg-config ];
          buildInputs = [ pkgs.SDL2 ] ++ extraBuildInputs;

          buildPhase = ''
            runHook preBuild
            $CXX -std=c++17 -O2 -Wall -Wextra \
              ${appDir}/main.cpp \
              $(pkg-config --cflags --libs sdl2) \
              ${pkgs.lib.concatStringsSep " " extraLdFlags} \
              -o ${appName}
            runHook postBuild
          '';

          installPhase = ''
            mkdir -p $out/bin
            cp ${appName} $out/bin/
          '';
        };

      hellogl = mkApk { appName = "hellogl"; appDir = ./apps/hellogl; };
      hellosdl = mkApk { appName = "hellosdl"; appDir = ./apps/hellosdl; };

      hellosdlLinux = mkDesktopApp { appName = "hellosdl"; appDir = ./apps/hellosdl; };
      helloglLinux = mkDesktopApp {
        appName = "hellogl";
        appDir = ./apps/hellogl;
        extraBuildInputs = [ pkgs.libglvnd ];
        extraLdFlags = [ "-lGLESv2" ];
      };
    in {
      packages.${system} = {
        inherit hellogl hellosdl sdlAndroidLibs;
        hellosdl-linux = hellosdlLinux;
        hellogl-linux = helloglLinux;
        default = hellogl;
      };

      apps.${system} = {
        install-hellogl = mkInstallApp hellogl "hellogl";
        install-hellosdl = mkInstallApp hellosdl "hellosdl";
        install = mkInstallApp hellogl "hellogl"; # default: the GL app

        run-hellosdl-linux = {
          type = "app";
          program = "${hellosdlLinux}/bin/hellosdl";
        };
        run-hellogl-linux = {
          type = "app";
          program = "${helloglLinux}/bin/hellogl";
        };
      };
    };
}
