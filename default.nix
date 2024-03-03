{ pkgs ? import <nixpkgs> {} }:
pkgs.stdenv.mkDerivation {

  name = "game";
  src = fetchGit {
    url = ./.;
    shallow = true;
  };

  nativeBuildInputs = with pkgs; [
    gnumake
    clang_17
    raylib
  ];

  buildPhase = ''
    make build
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp ./bin/game $out/bin
  '';

  shellHook = ''
  '';
}
