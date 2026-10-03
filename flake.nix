{
  # OmaPhoto: Compositor for Linux, built with CMake.
  description = "OmaPhoto, layered image editing and compositing for Linux";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  outputs = { self, nixpkgs }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
      # Remove Background's model, pinned by its checksum.
      model = pkgs.fetchurl {
        url = "https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net.onnx";
        sha256 = "8d10d2f3bb75ae3b6d527c77944fc5e7dcd94b29809d47a739a7a728a912b491";
      };
    in {
      packages.x86_64-linux.default = pkgs.stdenv.mkDerivation {
        pname = "omaphoto";
        version = "1.4.5";
        # dev.sh's build and the Swift spec stay out.
        src = pkgs.lib.cleanSourceWith {
          src = self;
          filter = path: type: !(builtins.elem path [ "${toString self}/build" "${toString self}/Compositor-Ref" ]);
        };
        nativeBuildInputs = with pkgs; [ cmake ninja pkg-config qt6.wrapQtAppsHook ];
        buildInputs = with pkgs; [ qt6.qtbase qt6.qtsvg qt6.qtimageformats libheif libde265 libraw fontconfig onnxruntime ];
        cmakeFlags = [ "-DOMAPHOTO_MODEL=${model}" ];
        ninjaFlags = [ "omaphoto" ];
        # Staged: profiles build MIME caches outside the store.
        installPhase = ''
          runHook preInstall
          DESTDIR=/ cmake --install . --prefix $out
          runHook postInstall
        '';
      };
      # The tests' shell: plugins, fonts and the model found at run time.
      devShells.x86_64-linux.default = pkgs.mkShell {
        inputsFrom = [ self.packages.x86_64-linux.default ];
        QT_PLUGIN_PATH = "${pkgs.qt6.qtimageformats}/lib/qt-6/plugins:${pkgs.qt6.qtbase}/lib/qt-6/plugins";
        FONTCONFIG_FILE = pkgs.makeFontsConf {
          fontDirectories = [ pkgs.dejavu_fonts ];
          # fontconfig's own rules, which NixOS keeps in /etc.
          includes = [ "${pkgs.fontconfig.out}/etc/fonts/conf.d" ];
        };
        # The MIME database too, which NixOS keeps in its profile.
        XDG_DATA_DIRS = "${pkgs.runCommand "u2net" { } "mkdir -p $out/share/omaphoto && ln -s ${model} $out/share/omaphoto/u2net.onnx"}/share:${pkgs.shared-mime-info}/share";
      };
    };
}
