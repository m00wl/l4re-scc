{
  description = "l4re-scc flake";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  };

  outputs = inputs @ {flake-parts, ...}:
    flake-parts.lib.mkFlake {inherit inputs;} {
      imports = [];
      systems = [ "x86_64-linux" ];
      perSystem = {
        config,
        self',
        inputs',
        pkgs,
        system,
        ...
      }: let
        pkgsArm64 = pkgs.pkgsCross.aarch64-multiplatform;
      in {
        devShells.default = pkgsArm64.mkShell {
          CROSS_COMPILE = "aarch64-unknown-linux-gnu-";
          buildInputs = [
            pkgs.ncurses
          ];
          packages = [
            # build
            pkgs.gcc
            pkgs.dialog
            pkgs.gawk
            pkgs.binutils
            pkgs.pkg-config
            pkgs.flex
            pkgs.bison
            pkgs.dtc
            pkgs.ubootTools
            # run
            pkgs.qemu
          ];
          shellHook = ''
            export L4RE_SCC_ROOT=$(git rev-parse --show-toplevel)
          '';
        };
      };
      flake = {};
    };

  nixConfig = {
    bash-prompt-suffix = "dev: ";
  };
}
