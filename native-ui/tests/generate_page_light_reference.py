"""Capture original PageTurn point-light spherical conversion and uniform packing."""
import argparse
import hashlib
import math
from pathlib import Path
import struct
from generate_page_material_reference import record


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference",type=Path)
    parser.add_argument("output",type=Path)
    args=parser.parse_args()
    image=args.reference.read_bytes()
    if hashlib.sha256(image).hexdigest()!="5a74caf8a0e7b67624c23ea95a67f6b2a79adfb8919352310330c2b10ce10181":
        raise ValueError("Unreviewed PAF reference")
    f=lambda bits:struct.unpack("<f",struct.pack("<I",bits))[0]
    angles=[0.,-0.,.1,-.1,.5,.65345126,.91734505,1.3,math.pi/2,math.pi,
        math.pi*1.5,math.tau,7.,-7.,10.,-10.,100.,-100.]
    # Probe each side of the original polynomial/quadrant branch thresholds.
    for bits in (0x3f274772,0x3f6ad720,0x3fc90fdb,0x40490fdb,0x40c90fdb):
        angles.extend(f(bits+offset) for offset in (-1,0,1))
    pairs=[(f(0x3fa8bf90),f(0x3f0f9d91)),(f(0x3fc90fd8),f(0x3e2c5693))]
    pairs.extend((angle,angles[-i-1]) for i,angle in enumerate(angles))
    records=[]
    for theta,phi in pairs:
        for distance,attenuation,ambient,diffuse,fade in (
            (f(0x450ba0a1),(1,0,0),(.2,.2,.2),(1,1,1),1),
            (0,(0,0,0),(0,0,0),(.3,.7,1),0),
            (1234.5,(.5,.002,.0001),(.1,.2,.4),(.8,.6,.3),.65)):
            inputs=struct.pack("<13f",theta,phi,distance,*attenuation,*ambient,*diffuse,fade)
            values=struct.unpack("<13f",inputs)
            expected=record(image,960,544,(1.6,.85),(.232,.608,.009,3.3),light_values=values,lights=True)
            records.append(inputs+struct.pack("<32I",*expected))
    args.output.write_bytes(b"PAFLIT01"+struct.pack("<I",len(records))+b"".join(records))
    print("Generated",len(records),"original page-light records")


if __name__=="__main__":
    main()
