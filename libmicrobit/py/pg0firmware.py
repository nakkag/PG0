from microbit import *
import radio
import music
uart.init(baudrate=115200,bits=8,parity=None,stop=1)
while True:
	try:
		if not uart.any():continue
		b=uart.read(1);r=uart.read()
		while b'\x01' not in r:
			if uart.any():r=r+uart.read()
		s=str(r[:-1],'UTF-8')
		if b==b'\x10':uart.write('3')
		elif b==b'\x11':uart.write(str(globals()[s]()))
		elif b==b'\x12':f=s.split(".");uart.write(str(getattr(globals()[f[0]],f[1])()))
		elif b==b'\x13':p=s.split(",");f=p[0].split(".");uart.write(str(getattr(globals()[f[0]],f[1])(int(p[1]))))
		elif b==b'\x20':
			if len(s)==1:display.show(s)
			else:display.scroll(s)
		elif b==b'\x21':
			display.show(Image(s))
		elif b==b'\x23':p=s.split(",");uart.write(str(display.get_pixel(int(p[0]),int(p[1]))))
		elif b==b'\x24':p=s.split(",");display.set_pixel(int(p[0]),int(p[1]),int(p[2]))
		elif b==b'\x37':
			compass.calibrate()
			r=1 if compass.is_calibrated()==True else 0
			uart.write(str(r))
		elif b==b'\x41':
			r=1 if accelerometer.was_gesture(s)==True else 0
			uart.write(str(r))
		elif b==b'\x50':radio.config(group=int(s))
		elif b==b'\x51':radio.config(power=int(s))
		elif b==b'\x52':radio.send(s)
		elif b==b'\x53':
			r=radio.receive()
			if r!=None:uart.write(r)
		elif b==b'\x60':music.pitch(int(s),wait=False)
		uart.write('\x01')
	except:uart.write('\x02\x01')
