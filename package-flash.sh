#!/bin/bash

zip -j swadge2024.zip build/bootloader/bootloader.bin build/swadge2024.bin build/partition_table/partition-table.bin build/storage.bin

# flash with (after unpacking the zip file)
# esptool.py --chip esp32s2 --port /dev/ttyACM0 --baud 2000000 write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB 0x1000 bootloader.bin 0x10000 swadge2024.bin 0x8000 partition-table.bin 0x208000 storage.bin