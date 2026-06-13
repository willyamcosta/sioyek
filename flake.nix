{
  inputs.nixpkgs.url = "https://flakehub.com/f/NixOS/nixpkgs/0";

  outputs =
    { self, nixpkgs, ... }:

    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
      ];

      forEachSupportedSystem =
        f:
        nixpkgs.lib.genAttrs supportedSystems (
          system:
          f {
            inherit system;
            pkgs = import nixpkgs {
              inherit system;
            };
          }
        );
    in
    {
      packages = forEachSupportedSystem (
        { pkgs, system }:
        {
          default = pkgs.callPackage ./package.nix {
            src = ./.;
          };

          sioyek = self.packages.${system}.default;
        }
      );

      devShells = forEachSupportedSystem (
        { pkgs, system }:
        {
          default = pkgs.mkShell {
            inputsFrom = [
              self.packages.${system}.default
            ];

            packages =
              with pkgs;
              [
                clang-tools
                codespell
                cppcheck
                doxygen
                git
                pkg-config
                self.formatter.${system}
              ]
              ++ lib.optionals (!stdenv.hostPlatform.isDarwin) [
                gdb
              ]
              ++ lib.optionals stdenv.hostPlatform.isDarwin [
                lldb
              ];

            shellHook = ''
              export QMAKE="${pkgs.qt6.qmake}/bin/qmake"
              echo "QMAKE=$QMAKE"
            '';
          };
        }
      );

      formatter = forEachSupportedSystem ({ pkgs, ... }: pkgs.nixfmt);
    };
}
