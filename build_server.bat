@echo off
setlocal

set "OUTPUT=server.exe"
set "COMMON_FLAGS=-std=c11 -Wall -Wextra -pedantic"

where gcc >nul 2>nul
if not errorlevel 1 (
  echo Building with gcc...
  gcc %COMMON_FLAGS% server.c -o %OUTPUT% -lws2_32
  goto :done
)

where clang >nul 2>nul
if not errorlevel 1 (
  echo Building with clang...
  clang %COMMON_FLAGS% server.c -o %OUTPUT% -lws2_32
  goto :done
)

echo No supported C compiler found.
echo Install MinGW-w64 or LLVM/Clang and make sure gcc or clang is available in PATH.
exit /b 1

:done
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

echo Build successful: %OUTPUT%
endlocal
