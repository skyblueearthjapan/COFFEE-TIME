"""ESP32 に開発用の 1 文字コマンドを送る。 使い方: python tools/send.py COM8 M"""
import sys
import time

import ctport

s = ctport.open_port(sys.argv[1])     # COM8 / net（Wi-Fi の遠隔コンソール）
s.write(sys.argv[2].encode())
s.flush()
time.sleep(0.5)
s.close()
