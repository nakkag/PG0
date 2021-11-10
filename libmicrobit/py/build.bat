cd "%~dp0"
python uflash.py pg0firmware.py hex
copy hex\micropython.hex ..\pg0firmware.hex
