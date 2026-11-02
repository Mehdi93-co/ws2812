@echo off
"D:\Projekte\Barometer\dfu-util\dfu-util-0.11-binaries\win64\dfu-util.exe" -a 0 -s 0x08000000:leave -D "%~dp0..\build\debug\build\WS2812Controller.bin"
pause
