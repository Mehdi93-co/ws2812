@echo off
rem Board in den Bootloader bringen: im Terminal "dfu" eingeben (oder BOOT0 + Reset).
rem -w wartet, bis das Board als DFU-Geraet auftaucht.
"D:\Projekte\Barometer\dfu-util\dfu-util-0.11-binaries\win64\dfu-util.exe" -w -a 0 -s 0x08000000:leave -D "%~dp0..\build\debug\build\WS2812Controller.bin"
pause
