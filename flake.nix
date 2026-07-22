{
  description = "Reproducible SDL2 + C++ HelloWorld APK for Fire HD 10 Gen7 (Fire OS 5 / Android 5.1, API 22)";

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
      appName = "helloworld";
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
    in {
      packages.${system}.default = pkgs.stdenvNoCC.mkDerivation {
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
          COMPILE_JAR=$ANDROID_HOME/platforms/android-${compilePlatform}/android.jar

          cp -r ${./src} src
          chmod -R u+w src

          cp ${./keystore/debug.keystore} debug.keystore

          # --- unpack SDL2 and vendor it into the ndk-build project tree ---
          mkdir -p work
          tar xzf ${sdlSrc} -C work
          mv work/SDL2-${sdlVersion} src/jni/SDL

          # --- copy SDL's own Java glue (SDLActivity et al.) verbatim ---
          mkdir -p javasrc
          cp -r src/jni/SDL/android-project/app/src/main/java/org javasrc/org
          chmod -R u+w javasrc

          # --- native build via ndk-build ---
          $NDK/ndk-build \
            NDK_PROJECT_PATH=$PWD/src \
            APP_BUILD_SCRIPT=$PWD/src/jni/Android.mk \
            NDK_APPLICATION_MK=$PWD/src/jni/Application.mk \
            -j"$NIX_BUILD_CORES"

          # --- compile the SDL Java glue classes ---
          mkdir -p classes
          javac -encoding UTF-8 --release 8 -classpath "$COMPILE_JAR" -d classes \
            $(find javasrc -name '*.java')

          $BT/d8 --output classes --min-api ${packagePlatform} $(find classes -name '*.class')

          # --- package resources + manifest ---
          mkdir -p out
          $BT/aapt package -f \
            -M src/AndroidManifest.xml \
            -I "$PACKAGE_JAR" \
            -F out/base.apk

          # --- add dex + native libraries ---
          cp classes/classes.dex out/classes.dex
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
          cp out/${appName}.apk $out/
        '';
      };

      apps.${system}.install = {
        type = "app";
        program = toString (pkgs.writeShellScript "adb-install" ''
          exec ${pkgs.android-tools}/bin/adb install -r ${self.packages.${system}.default}/${appName}.apk
        '');
      };
    };
}
