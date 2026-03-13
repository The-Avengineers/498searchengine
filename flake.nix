{
  description = "498 Search Engine (avengine)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        # `nix develop` shell — provides everything needed for `make configure/build/test`
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
            clang-tools   # clang-tidy, clangd, clang-format

            # Required libraries
            openssl
            zlib

            # Test framework (found via find_package(doctest CONFIG))
            doctest
          ];

          # Help CMake find Nix-managed packages
          shellHook = ''
            export CMAKE_PREFIX_PATH="${pkgs.openssl.dev}:${pkgs.zlib.dev}:${pkgs.doctest}''${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
          '';
        };

        # `nix build` — produces the avengine static library + executables
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "avengine";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = with pkgs; [ cmake ninja ];
          buildInputs = with pkgs; [ openssl zlib ];

          cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" "-DBUILD_TESTING=OFF" ];
        };
      }
    );
}
