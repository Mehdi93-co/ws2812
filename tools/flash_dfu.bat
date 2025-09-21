@echo off
cd /d E:\dfu-util\dfu-util-0.9-win64
dfu-util.exe -a 0 -s 0x08000000:leave -D E:\Projekte\WS2812\WS2812Controller\build\debug\build\WS2812Controller.bin
pause