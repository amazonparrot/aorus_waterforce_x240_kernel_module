import usb.core
import usb.util
import psutil
import time
import hid
import usb1
#c9 08 64 0a 08 01
#99 cd r g b
#E5 fan/pump mode
#07 zero
#00 balance
#05 performance
#06 quiet
#04 max
#02 default
#01 custom
#set fan mode
#99 e5 01(01-fan/02-pump) 02(mode)
#set custom fan curve 
#99 e6 01 01 00(degree) 03 ef(speed 1007)  
#set custom pump curve
#99 e6 04 02 00(degree) 03 f3(speed 1010)
#99 d9 01 (read fan curve)
#99 d9 02 (read pump curve)

#99 da (read cooler stat)
#(cooler stat return)
#99 da 62 03(fan speed) 00 f5 04(pump speed) 00 [20 25 1f a1] 00 1a(liquid deg)
def replay(dev):
    def bulkRead(endpoint, length, timeout=None):
        return dev.bulkRead(endpoint, length, timeout=(1000 if timeout is None else timeout))

    def bulkWrite(endpoint, data, timeout=None):
        dev.bulkWrite(endpoint, data, timeout=(1000 if timeout is None else timeout))
    
    def controlRead(bRequestType, bRequest, wValue, wIndex, wLength,
                    timeout=None):
        return dev.controlRead(bRequestType, bRequest, wValue, wIndex, wLength,
                    timeout=(1000 if timeout is None else timeout))

    def controlWrite(bRequestType, bRequest, wValue, wIndex, data,
                     timeout=None):
        dev.controlWrite(bRequestType, bRequest, wValue, wIndex, data,
                     timeout=(1000 if timeout is None else timeout))

    def interruptRead(endpoint, size, timeout=None):
        return dev.interruptRead(endpoint, size,
                    timeout=(1000 if timeout is None else timeout))

    def interruptWrite(endpoint, data, timeout=None):
        dev.interruptWrite(endpoint, data, timeout=(1000 if timeout is None else timeout))

def open_dev(usbcontext=None):
    if usbcontext is None:
        usbcontext = usb1.USBContext()
    
    print('Scanning for devices...')
    for udev in usbcontext.getDeviceList(skip_on_error=True):
        vid = udev.getVendorID()
        pid = udev.getProductID()
        if (vid, pid) == (0x1044, 0x7a4d):
            print("")
            print("")
            print('Found device')
            print('Bus %03i Device %03i: ID %04x:%04x' % (
                udev.getBusNumber(),
                udev.getDeviceAddress(),
                vid,
                pid))
            return udev.open()
    raise Exception("Failed to find a device")

def setupusb1():
  usbcontext = usb1.USBContext()
  dev = open_dev(usbcontext)
#  dev.releaseInterface(0)
  dev.claimInterface(0)
  dev.resetDevice()
  return dev

def get_cpu_temp():
    t = psutil.sensors_temperatures()
    for x in ['coretemp']:
        if x in t:
            return int(t[x][0].current)
    print("Warning: Unable to get CPU temperature!")
    return 0 
def makefanorpumpmodestr(fanorpump,mode):
  
  data="99e5"
  if fanorpump=="fan": data+="01"
  if fanorpump=="pump": data+="02"
  modedict={"balance":"00","custom":"01","default":"02","max":"04","performance":"05","quiet":"06","zero":"07"}
  data+=modedict[mode]
  data+="0"*(6144*2-len(data))
  barray=bytes.fromhex(data)
  return barray
def makefansetcolorstr(color):
  data="99cd"
  data+=color
  data+="0"*(6144*2-len(data))
  barray=bytes.fromhex(data)
  return barray
  
def makestatusrequeststr():
  data="99da"
  data+="0"*(6144*2-len(data))
  barray=bytes.fromhex(data)
  return barray
def makecpuupdatestr():
  #change to your number of core 
  ncore=24
  #change to your number of threads
  nthread=32
  #change to your cpu Ghz
  ghz=5.5
  cput=get_cpu_temp()
  cputemp="{:02x}".format(cput)
  ncorestr="{:02x}".format(ncore)
  nthreadstr="{:02x}".format(nthread)
  ghz1="{:02x}".format(int(ghz))
  ghz2="{:02x}".format(int( (ghz-int(ghz))*10) )
#  print(ghz1)
#  print(ghz2)
  data="99e000"+cputemp+nthreadstr+ghz1+ghz2+ncorestr+"30"
#  print(data)
  data+="0"*(6144*2-len(data))
  barray=bytes.fromhex(data)
  print("cputemp",cput)
  return barray
def readhexdump(filename):
  f=open(filename,"r")
  data=f.read()
  barray=bytes.fromhex(data)
  return barray
def printstatus(data):
  datahex=data.hex()
#  print(datahex)  
  fanspeed=int(datahex[6:8]+datahex[4:6],base=16)
  pumpspeed=int(datahex[12:14]+datahex[10:12],base=16)
  pumptemp=int(datahex[26:28],base=16)
  print("fanspeed",fanspeed,"pumpspeed",pumpspeed,"pumptemp",pumptemp)
  return fanspeed,pumpspeed,pumptemp
#testusb1()
#exit()
# find our device
dev = usb.core.find(idVendor=0x1044, idProduct=0x7a4d)

# was it found?
#if dev is None:
#    raise ValueError('Device not found')

#bdata=makecpuupdatestr()
#print(dev)
#endpoint = dev[0][(0,0)][0]
#endpoint2 = dev[0][(0,0)][1]
#endpoint3 = dev[0][(0,0)][2]
#print(endpoint)
#print(endpoint2)
#print(endpoint3)
#exit()
interface=0
if dev.is_kernel_driver_active(interface) is True:
#  # tell the kernel to detach
  dev.detach_kernel_driver(interface)
#  usb.util.claim_interface(dev, interface)
#bdata=makefanorpumpmodestr("fan","balance")
#dev.write(endpoint2,bdata)
#bdata=makefanorpumpmodestr("pump","balance")
#dev.write(endpoint2,bdata)
dev=setupusb1()
bdata=makefanorpumpmodestr("fan","balance")
dev.interruptWrite(0x2,bdata)
bdata=makefanorpumpmodestr("pump","balance")
dev.interruptWrite(0x2,bdata)
while(True):
 try:
     bdata=makecpuupdatestr()
     dev.interruptWrite(0x2,bdata)
     bdata=makestatusrequeststr()
     dev.interruptWrite(0x2,bdata)
     ret=dev.interruptRead(0x81,256)

     fanspeed,pumpspeed,pumptemp=printstatus(ret)
     r="{:02x}".format(fanspeed%256)
     g="{:02x}".format(pumpspeed%256)
     b="{:02x}".format(pumptemp%256)
     print(r+g+b)
     bdata=makefansetcolorstr(r+g+b)
     dev.interruptWrite(0x2,bdata)

 except usb.core.USBError as e:
        print(e)
 time.sleep(2)
