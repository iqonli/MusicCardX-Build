"""Executed on the Windows Actions runner, never on a developer's Config."""
import ctypes as c
import os
from pathlib import Path
root=Path('build/windows/x64/runner/Release').resolve()
with os.add_dll_directory(str(root)):
    lib=c.WinDLL(str(root/'mcx_background.dll'))
    lib.mcx_render.argtypes=[c.c_int,c.c_void_p,c.c_size_t,c.c_double,c.c_double,c.c_int,c.c_int,c.c_void_p,c.c_void_p,c.c_void_p,c.c_int,c.c_int,c.c_void_p,c.c_size_t,c.c_int]
    lib.mcx_render.restype=c.c_int
    colors=(c.c_uint32*5)(0x182830,0x4a8090,0xac8060,0xccccaa,0x223040)
    for mode in range(4):
        for seed in [b'12345678',b'98765432']:
            outputs=[]
            for threads in [1,4]:
                out=(c.c_ubyte*(32*18*4))()
                assert lib.mcx_render(mode,seed,len(seed),1920,1080,32,18,colors,None,None,0,0,out,len(out),threads)==0
                outputs.append(bytes(out))
            assert outputs[0]==outputs[1],(mode,seed)
            assert all(outputs[0][i]==255 for i in range(3,len(outputs[0]),4))
print('Windows DLL loaded; four modes, two seeds, single/multi-thread reproducibility passed.')
