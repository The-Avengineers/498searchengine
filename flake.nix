{
  description = "498 Search Engine (avengine)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # pkgsStatic targets x86_64-unknown-linux-musl and compiles every
        # library dependency as a static archive (.a).  The resulting
        # executables have zero runtime library dependencies and run on any
        # x86_64 Linux userland — including the GCP Debian 13 instances
        # created by create_vm.sh.
        pkgsStatic = pkgs.pkgsStatic;

        # ---------------------------------------------------------------------------
        # Shared install phase — copies every built executable into $out/bin.
        # CMakeLists.txt does not define install() rules, so we do it here.
        # ---------------------------------------------------------------------------
        installExes = ''
          runHook preInstall
          mkdir -p $out/bin
          for exe in crawler_main search_server index_builder index_reader probing_benchmark; do
            [ -f "$exe" ] && install -m755 "$exe" "$out/bin/$exe"
          done
          runHook postInstall
        '';

      in
      {

        # ---------------------------------------------------------------------------
        # nix develop  — local dev shell (make configure / make build / make test)
        # ---------------------------------------------------------------------------
        devShells.default = pkgs.mkShell {
          name = "avengine-dev";

          packages = with pkgs; [
            # Build tools
            cmake
            gnumake
            ninja

            # C/C++ toolchain
            gcc
            gdb
            clang-tools # clang-tidy, clangd, clang-format

            # Runtime library deps
            openssl
            zlib

            # Test framework  (find_package(doctest CONFIG))
            doctest
          ];

          shellHook = ''
            export CMAKE_PREFIX_PATH="${pkgs.openssl.dev}:${pkgs.zlib.dev}:${pkgs.doctest}''${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
          '';
        };

        packages = {

          # -------------------------------------------------------------------------
          # nix build  (default)
          #
          # Dynamically linked native build.  Links against the Nix-store copy of
          # glibc, so it only runs reliably on this NixOS machine.  Use this for
          # rapid local iteration; use .#static for anything shipped to GCP.
          # -------------------------------------------------------------------------
          default = pkgs.stdenv.mkDerivation {
            pname = "avengine";
            version = "0.1.0";
            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              ninja
            ];
            buildInputs = with pkgs; [
              openssl
              zlib
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
              "-DBUILD_TESTING=OFF"
            ];

            installPhase = installExes;
          };

          # -------------------------------------------------------------------------
          # nix build .#static
          #
          # Fully static build for GCP Debian deployment.
          #
          # pkgsStatic compiles openssl and zlib against musl libc and produces
          # .a archives, so the final executables are self-contained — no shared
          # libraries need to be present on the target machine.
          #
          # Build:
          #   nix build .#static
          #
          # The binaries land in ./result/bin/:
          #   crawler_main  search_server  index_builder  index_reader  probing_benchmark
          #
          # Deploy to GCP (example):
          #   gcloud compute scp ./result/bin/search_server \
          #       instance-20260403-191526:/usr/local/bin/ --zone=us-central1-b
          # -------------------------------------------------------------------------
          static = pkgsStatic.stdenv.mkDerivation {
            pname = "avengine-static";
            version = "0.1.0";
            src = ./.;

            # cmake and ninja execute on the build host, so pull them from the
            # host pkgs — not from pkgsStatic (which would give musl-linked tools).
            nativeBuildInputs = with pkgs; [
              cmake
              ninja
            ];

            # openssl and zlib come from pkgsStatic: they are compiled as .a
            # archives against musl.  CMake's find_package() picks them up via
            # the CMAKE_PREFIX_PATH that stdenv injects automatically.
            buildInputs = with pkgsStatic; [
              openssl
              zlib
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
              "-DCMAKE_CXX_FLAGS='-fno-omit-frame-pointer'"
              # Prefer .a archives so no shared library slips in through
              # find_package / find_library calls.
              "-DCMAKE_FIND_LIBRARY_SUFFIXES=.a"
            ];

            installPhase = installExes;
          };

        };
      }
    );
}
