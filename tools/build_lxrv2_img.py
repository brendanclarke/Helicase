#!/usr/bin/env python3
import sys,struct
def build(b,o):
    p=open(b,"rb").read();c=(~sum(p))&0xFF
    open(o,"wb").write(b"LXRV2IMG"+struct.pack("<II",len(p),c)+p)
    print(f"Written:{o} ({len(p)}b) {'OK' if (sum(p)+c)&0xFF==0xFF else 'FAIL'}")
if __name__=="__main__":
    if len(sys.argv)!=3:print(f"Usage:{sys.argv[0]} in.bin out.img");sys.exit(1)
    build(sys.argv[1],sys.argv[2])
