"""ESP32 に開発用の 1 文字コマンドを送る。 使い方: python tools/send.py COM8 M"""
import sys
import time

import serial

s = serial.Serial()
s.port = sys.argv[1]
s.baudrate = 115200
s.dtr = False
s.rts = False
s.open()
s.write(sys.argv[2].encode())
s.flush()
time.sleep(0.5)
s.close()
