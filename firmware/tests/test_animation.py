#!/usr/bin/env python3
"""Compile the real C decoder and verify every embedded frame against GIF pixels."""
import ctypes, importlib.util, struct, subprocess, tempfile
from pathlib import Path
from PIL import Image, ImageSequence

root=Path(__file__).resolve().parents[1]
def test(gif_path):
    spec=importlib.util.spec_from_file_location('pack_animation',root/'pack_animation.py')
    pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)
    class Animation(ctypes.Structure):
        _fields_=[('data',ctypes.POINTER(ctypes.c_uint8)),('size',ctypes.c_size_t),
                  ('width',ctypes.c_uint16),('height',ctypes.c_uint16),
                  ('fps',ctypes.c_uint16),('count',ctypes.c_uint32)]
    with tempfile.TemporaryDirectory() as temp:
        libpath=Path(temp)/'animation.so'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC',str(root/'main/animation.c'),'-o',str(libpath)],check=True)
        lib=ctypes.CDLL(str(libpath))
        lib.bober_animation_open.argtypes=[ctypes.POINTER(Animation),ctypes.POINTER(ctypes.c_uint8),ctypes.c_size_t]
        lib.bober_animation_open.restype=ctypes.c_bool
        lib.bober_animation_decode.argtypes=[ctypes.POINTER(Animation),ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint8),ctypes.c_size_t]
        lib.bober_animation_decode.restype=ctypes.c_bool
        raw=(root/'main/assets/loop.banim').read_bytes()
        buf=(ctypes.c_uint8*len(raw)).from_buffer_copy(raw);anim=Animation()
        assert lib.bober_animation_open(ctypes.byref(anim),buf,len(raw))
        dst=(ctypes.c_uint8*32768)()
        with Image.open(gif_path) as gif:
            assert anim.count==gif.n_frames and anim.fps==15
            for index,frame in enumerate(ImageSequence.Iterator(gif)):
                quant=frame.convert('RGB').quantize(colors=64,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
                expected=bytearray()
                for r,g,b in quant.convert('RGB').getdata():
                    expected.extend(struct.pack('>H',((r>>3)<<11)|((g>>2)<<5)|(b>>3)))
                assert lib.bober_animation_decode(ctypes.byref(anim),index,dst,len(dst)),index
                assert bytes(dst)==expected,index
        assert not lib.bober_animation_decode(ctypes.byref(anim),anim.count,dst,len(dst))
        assert not lib.bober_animation_decode(ctypes.byref(anim),0,dst,32767)
        # Every frame must reject truncated or out-of-range payloads.
        for offset in [0,4,6,8,10,12,14,18,22]:
            bad=bytearray(raw);bad[offset]^=0xff
            b=(ctypes.c_uint8*len(bad)).from_buffer_copy(bad)
            if lib.bober_animation_open(ctypes.byref(anim),b,len(bad)):
                assert not lib.bober_animation_decode(ctypes.byref(anim),0,dst,len(dst))
        assert not lib.bober_animation_open(ctypes.byref(anim),buf,17)
        assert not lib.bober_animation_open(ctypes.byref(anim),buf,len(raw)-1)
        print(f'PASS: {gif.n_frames} real C-decoded frames, exact RGB565 colors, bounds and malformed headers')

if __name__=='__main__':
    import sys
    test(sys.argv[1])
