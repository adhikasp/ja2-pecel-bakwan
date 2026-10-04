"""HLSL -> DXBC with the system's d3dcompiler_47.dll (Windows only):
dxbc.py <in.hlsl> <out.dxbc> [profile] [entry]. Profiles are shader model 5.1 (cs_5_1, vs_5_1, ps_5_1);
the default is cs_5_1/main. Used by build.sh; SDL_GPU's D3D12 backend takes DXBC."""
import ctypes
import sys

src, dst = sys.argv[1], sys.argv[2]
profile = sys.argv[3] if len(sys.argv) > 3 else "cs_5_1"
entry = sys.argv[4] if len(sys.argv) > 4 else "main"
code = open(src, "rb").read()
d3d = ctypes.WinDLL("d3dcompiler_47.dll")


class Blob(ctypes.Structure):
    pass


BlobPtr = ctypes.POINTER(ctypes.c_void_p)


def blob_bytes(blob):
    vtbl = ctypes.cast(blob, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    get_ptr = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vtbl[3])
    get_size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vtbl[4])
    return ctypes.string_at(get_ptr(blob), get_size(blob))


out = ctypes.c_void_p()
err = ctypes.c_void_p()
D3DCOMPILE_OPTIMIZATION_LEVEL3 = 1 << 15
hr = d3d.D3DCompile(code, len(code), src.encode(), None, None, entry.encode(), profile.encode(),
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, ctypes.byref(out), ctypes.byref(err))
if hr != 0:
    msg = blob_bytes(err).decode(errors="replace") if err.value else ""
    sys.exit("D3DCompile failed (0x%08x): %s" % (hr & 0xFFFFFFFF, msg))
open(dst, "wb").write(blob_bytes(out))
