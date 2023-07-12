{ pkgs ? import <nixpkgs> {} }:
with pkgs; stdenv.mkDerivation {
  name = "hi";
  buildInputs = [ xorg.libX11 xorg.libXi ];
}
