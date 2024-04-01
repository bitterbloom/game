{ pkgs ? import <nixpkgs> {} }:
pkgs.stdenv.mkDerivation {

  name = "game";
  src = fetchGit {
    url = ./.;
    shallow = true;
  };

  nativeBuildInputs = with pkgs; [
    gnumake
    gcc13
    raylib
  ];

  buildPhase = ''
    make build
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp ./bin/main-build $out/bin
  '';

  shellHook = ''
  '';
}
