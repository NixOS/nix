@echo off

rem path to old nix (mingw's is ok)
set OLDNIX=C:\nix2
set NIX_STORE_DIR=C:\nix\store
set NIX_PATH=nixpkgs=C:\msys64\home\User\nixpkgs

for /f %%i in ('%OLDNIX%\bin\nix-build.exe --no-out-link -E "(import <nixpkgs> { }).stdenv.cc"') do set STDENV_CC=%%i
for /f %%i in ('%OLDNIX%\bin\nix-build.exe --no-out-link -E "(import <nixpkgs> { }).boost"'    ) do set BOOST=%%i
for /f %%i in ('%OLDNIX%\bin\nix-build.exe --no-out-link -E "(import <nixpkgs> { }).openssl"'  ) do set OPENSSL=%%i
for /f %%i in ('%OLDNIX%\bin\nix-build.exe --no-out-link -E "(import <nixpkgs> { }).xz"'       ) do set XZ=%%i
for /f %%i in ('%OLDNIX%\bin\nix-build.exe --no-out-link -E "(import <nixpkgs> { }).bzip2"'    ) do set BZIP2=%%i
for /f %%i in ('%OLDNIX%\bin\nix-build.exe --no-out-link -E "(import <nixpkgs> { }).curl"'     ) do set CURL=%%i
for /f %%i in ('%OLDNIX%\bin\nix-build.exe --no-out-link -E "(import <nixpkgs> { }).sqlite"'   ) do set SQLITE=%%i
echo STDENV_CC=%STDENV_CC%
echo BOOST=%BOOST%
echo OPENSSL=%OPENSSL%
echo XZ=%XZ%
echo BZIP2=%BZIP2%
echo CURL=%CURL%
echo SQLITE=%SQLITE%

rem PATH=%STDENV_CC%\bin;%PATH%

rem %STDENV_CC%\bin\nmake /E -f Makefile.win clean
%STDENV_CC%\bin\nmake /E -f Makefile.win install
