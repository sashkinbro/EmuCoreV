"""Capture page model/MVP matrices from the original renderer's CPU assembly.

Uses the reviewed host from generate_page_material_reference. Original rotation,
origin placement, scaling and matrix composition execute without replacements.
"""
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
    identity=(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)
    records=[]
    for width,height in ((960,544),(896,448),(840,512),(1234,567)):
        poses=((1,1,1,width*.5,height*.5,0,0,0,0,1),
            (.8,1.2,.7,width*.5+30,height*.5-20,5,0,0,math.sin(.31),math.cos(.31)),
            (1.1,.9,1.3,300,250,-15,.1,.2,.3,.9),
            (1,1,1,480,270,0,0,0,0,0),
            (.75,.75,.5,500,300,20,-.2,.3,-.4,.8))
        ortho=(2/width,0,0,0,0,2/height,0,0,0,0,.001,0,-1,-1,0,1)
        translated=(1,0,0,0,0,1,0,0,0,0,1,0,-10,20,-3,1)
        view=(.9,.1,0,0,-.1,.9,0,0,0,0,1,0,12,-8,3,1)
        for pose in poses:
            for projection,camera in ((identity,identity),(ortho,translated),(ortho,view)):
                inputs=struct.pack("<44f",width,height,*pose,*projection,*camera)
                v=struct.unpack("<44f",inputs)
                expected=record(image,*v[:2],(1.6,.85),(.232,.608,.009,3.3),
                    pose=v[2:12],projection=v[12:28],view=v[28:44],matrices=True)
                records.append(inputs+struct.pack("<32I",*expected))
    args.output.write_bytes(b"PAFPOS01"+struct.pack("<I",len(records))+b"".join(records))
    print("Generated",len(records),"original page-transform records")


if __name__=="__main__":
    main()
