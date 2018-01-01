#!/bin/sh
MKSPIFFS="./mkspiffs"
ESPTOOL="/Users/bjy/.platformio/packages/framework-arduinoespressif32/tools/esptool.py"

$MKSPIFFS -p 256 -b 4096 -s 0x16f000 -c data spiffs.bin
 $ESPTOOL --chip esp32 --port /dev/cu.SLAB_USBtoUART  --baud 115200 write_flash -z 0x291000 spiffs.bin
