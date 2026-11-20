"""Linear disassembly of the 9.5b WCC386 image (linear base 0x1000), resuming after data bytes."""
import capstone, pickle, sys
img=open('.work/wcc/wcc386_95b.img','rb').read(); B=0x1000
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
out=[]; off=0; lim=int(sys.argv[1],16)-B if len(sys.argv)>1 else 0x5f000
while off<lim:
    got=False
    for i in md.disasm(img[off:lim],B+off):
        out.append((i.address,i.size,i.mnemonic,i.op_str)); off=i.address-B+i.size; got=True
    if not got: off+=1
    elif off<lim and out[-1][0]+out[-1][1]-B==off: off+=1
pickle.dump(out,open('.work/wcc/ins.pkl','wb')); print(len(out),'instructions')
