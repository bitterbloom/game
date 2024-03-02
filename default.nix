{ pkgs ? import <nixpkgs> {} }:
pkgs.stdenv.mkDerivation {

  name = "multiplayer-game";
  src = ./multiplayer-game;

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
    cp ./bin/multiplayer-game $out/bin
  '';

  shellHook = ''
  '';
}
