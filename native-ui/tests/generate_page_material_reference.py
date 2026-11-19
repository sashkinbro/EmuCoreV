"""Record common PageTurn material assembly from the reviewed original renderer.

The original renderer's parameter assembly executes, including fold-plane,
quaternion/matrix operations, lighting math and material adaptation. Graphics
and mesh submission, surfaces and uniform setters are hosted; the mesh itself
has a separate original-instruction comparison. Inputs are controlled test
styles/page poses, not a claim about the final shell's lock-screen configuration.
No reference instructions or host VM are linked into the native UI.
"""
import argparse
import hashlib
import struct
import zlib
from pathlib import Path
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC,
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP,
    UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPSCR)


def record(b, width, height, point, style, *, pose=None, projection=None, view=None,
        matrices=False, light_values=None, lights=False, material_values=None, material_pack=False):
    u=Uc(UC_ARCH_ARM,UC_MODE_THUMB)
    ph=struct.unpack_from('<Q',b,72)[0]; sec=struct.unpack_from('<Q',b,88)[0]
    for i in range(2):
     p=struct.unpack_from('<8I',b,ph+32*i)
     o,n,c,e=struct.unpack_from('<4Q',b,sec+32*i)
     if e!=2: raise ValueError('Reference must be decrypted')
     data=b[o:o+n]; data=zlib.decompress(data) if c==2 else data
     u.mem_map(p[2],(max(p[5],len(data))+4095)&~4095);u.mem_write(p[2],data)
    u.mem_map(0x10000,4096);u.mem_map(0x100000,65536);u.mem_map(0x200000,0x100000)
    u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);u.reg_write(UC_ARM_REG_FPEXC,0x40000000)
    def word(a,v):u.mem_write(a,struct.pack('<I',v))
    def fs(a,*v):u.mem_write(a,struct.pack('<'+'f'*len(v),*v))
    def matrix(a,x=1,y=1):fs(a,x,0,0,0,0,y,0,0,0,0,1,0,0,0,0,1)
    # Graphics state/program begin/end, surface references, draw submission,
    # deletion and the already separately verified mesh submission boundary.
    stubs=(0x81053778,0x810760c8,0x8107668c,0x81076744,0x810768c0,
        0x810768d4,0x81076936,0x81076a50,0x8107963a,0x81079722,
        0x8107a16a,0x8107a260,0x8107d07c,0x8108995c,0x8108997a,
        0x8108c806,0x8108c81c,0x810c71e0,0x811473f8,0x81147568)
    for a in stubs:u.mem_write(a,bytes.fromhex('7047'))
    for i in range(24):
     a=0x100100+i*16;u.mem_write(a,bytes.fromhex('7047'));word(0x260000+i*4,a|1)
    renderer,bundle,gl,data,surface,mesh=0x200000,0x204000,0x210000,0x230000,0x240000,0x250000
    # Host identity view/projection, centered page, identity rotation and an
    # unrounded surface select the flat material interface. All scalar styles
    # and material adaptation still pass through the original CPU renderer.
    word(renderer,bundle)
    for offset in (0x14,0x2bc,0x39c,0x604,0x74c,0xb14,0xc9c,0xf44,0xfbc,0x1384):word(bundle+offset,0x260000)
    for offset,stack in ((0,0x220000),(0x20,0x221000),(0x40,0x222000)):
     word(gl+offset,stack);word(gl+offset+0x10,1);matrix(stack);matrix(stack+64)
    if projection is not None: fs(0x220000,*projection)
    if view is not None: fs(0x222000,*view)
    u.mem_write(gl+0xc0,b'\x01\x01')
    word(data,surface);fs(data+8,width,height,0,0)
    fs(data+0x18,0,0);fs(data+0x20,1,1,1,1)
    fs(data+0x30,0,0,width,height);fs(data+0x40,width*.5,height*.5,0,1);fs(data+0x50,0,0,0,1)
    if pose is not None:
        fs(data+0x20,*pose[:3],1)
        fs(data+0x40,*pose[3:6],1)
        fs(data+0x50,*pose[6:10])
    fs(data+0x60,*point);u.mem_write(data+0x68,b'\x01')
    fs(surface+0x30,1/width,1/height)
    if material_values is None:
        material_values=(1.0,1.0,1.0,1.0,0x3f649247,0x3f8879ee,0x4185f15e,
            0x3da491b0,0x3f4d30ad,0x3e9349e0,0x404321c9,0x3ec49214,0x3dea454e)
    if len(material_values)!=13: raise ValueError('Material input must contain color4 plus nine scalars')
    fs(renderer+0x70*4+8,*material_values[:4])
    material=tuple(struct.unpack('<I',struct.pack('<f',value))[0] if isinstance(value,float) else value
        for value in material_values[4:])
    for i,v in enumerate(material):word(renderer+0x70*4+0x24+i*12,v)
    style=(style[0],style[1],.08,style[2],250,.3,style[3],1,16.2,.48,.3)
    for i,v in enumerate(style):fs(renderer+(0x209+i*3)*4,v)
    fs(renderer+0x25a*4,1,1,1,1)
    light=renderer+0xee*4
    fs(light+8,1.3,.5);fs(light+0x1c,2200);fs(light+0x50,.2,.2,.2,1);fs(light+0x70,1,1,1,1);fs(light+0x8c,1)
    if light_values is not None:
        fs(light+8,*light_values[:2]);fs(light+0x1c,light_values[2])
        fs(light+0x30,*light_values[3:6],0)
        fs(light+0x50,*light_values[6:9],1)
        fs(light+0x70,*light_values[9:12],1);fs(light+0x8c,light_values[12])
    fs(renderer+5*4,5)
    trace=[];seen=[]
    def hook(vm,a,n,_):
     seen.append(a)
     if len(seen)>16:seen.pop(0)
     if a==0x810760c8:vm.reg_write(UC_ARM_REG_R0,gl)
     elif 0x100100<=a<0x100280:
      obj=vm.reg_read(UC_ARM_REG_R0);src=vm.reg_read(UC_ARM_REG_R1)
      try:raw=bytes(vm.mem_read(src,64)); values=struct.unpack('<16I',raw)
      except UcError:values=[]
      trace.append({'object':hex(obj-bundle),'method':hex((a-0x100100)//4),'source':hex(src),'words':values})
    u.hook_add(UC_HOOK_CODE,hook)
    u.reg_write(UC_ARM_REG_R0,renderer);u.reg_write(UC_ARM_REG_R1,data);u.reg_write(UC_ARM_REG_R2,mesh)
    u.reg_write(UC_ARM_REG_SP,0x10f000);u.reg_write(UC_ARM_REG_LR,0x10001);u.reg_write(UC_ARM_REG_FPSCR,0)
    try:u.emu_start(0x810ca2f9,0x10000,count=3000000)
    except UcError as e:print(e,'pc',hex(u.reg_read(UC_ARM_REG_PC)),'last',[hex(x)for x in seen]);raise

    if u.reg_read(UC_ARM_REG_PC)!=0x10000:
        raise ValueError('Original material renderer did not return')
    def words(method):
        matches=[t['words'] for t in trace if t['object']=='0xc9c' and t['method']==hex(method)]
        if len(matches)!=1 or len(matches[0])!=16:
            raise ValueError('Unexpected original material setter sequence')
        return matches[0]
    if lights:
        return words(0x34)+words(0x30)
    if material_pack:
        return words(0x2c)
    if matrices:
        return words(0xc)[:16]+words(0x8)[:16]
    return (words(0x1c)[:4]+words(0x10)[:2]+words(0x18)[:4]+words(0x14)[:4]
        +words(0x20)[:4]+words(0x24)[:1]+words(0x2c)[9:11])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    image=args.reference.read_bytes()
    if hashlib.sha256(image).hexdigest()!='5a74caf8a0e7b67624c23ea95a67f6b2a79adfb8919352310330c2b10ce10181':
        raise ValueError('Unreviewed PAF reference')
    f=lambda bits:struct.unpack('<f',struct.pack('<I',bits))[0]
    styles=((f(0x3e6de871),f(0x3f1b6da8),f(0x3c1246bf),f(0x40536db5)),(.18,.95,.012,4))
    records=[]
    for width,height in ((960,544),(896,448),(840,512),(1234,567)):
        aspect=width/height
        points=((aspect-.12,.85),(aspect-.02,.99),(aspect,1),(aspect+.1,1.1),
            (0,0),(-aspect,-1),(.5*aspect,.5),(.1*aspect,1.2),(2*aspect,-.5),(-.1*aspect,.9))
        for style in styles:
            for point in points:
                inputs=struct.pack('<8f',width,height,*point,*style)
                v=struct.unpack('<8f',inputs)
                expected=record(image,*v[:2],v[2:4],v[4:])
                if len(expected)!=21: raise ValueError('Invalid material record length')
                records.append(inputs+struct.pack('<21I',*expected))
    args.output.write_bytes(b'PAFMAT01'+struct.pack('<I',len(records))+b''.join(records))
    print('Generated',len(records),'original CPU renderer material records')


if __name__=='__main__':
    main()
