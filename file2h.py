#!/usr/bin/python3
import os
import sys
import gzip
try:filename = sys.argv[1]
except:sys.exit(".bin file required\n\nExample: bin2h goldhen.bin")
cnt = 0
filename2 = filename.replace(".", "_")
tmpdat = "#if INTHEN\n#define INTHEN_NAME \"" + filename + "\"\nstatic const uint8_t "+filename2+" PROGMEM = {\n"
with open(filename, 'rb') as f:
    chnk = f.read(1)
    while chnk:
        if cnt == 31:
            cnt = 0
            tmpdat = tmpdat + "%s,\n" % ord(chnk)
        else:    
            tmpdat = tmpdat + "%s, " % ord(chnk)
        cnt=cnt+1
        chnk = f.read(1)
if tmpdat.endswith(","):
  tmpdat = tmpdat[:-1]
elif tmpdat.endswith(", "):
  tmpdat = tmpdat[:-2]
elif tmpdat.endswith(",\n"):
  tmpdat = tmpdat[:-2]
tmpdat = tmpdat + "\n};\n#endif"
f.close()
if os.path.exists("headerfile.h"):
  os.remove("headerfile.h")
f = open("headerfile.h", 'w+', encoding="utf-8") 
f.write(tmpdat)
f.close()
