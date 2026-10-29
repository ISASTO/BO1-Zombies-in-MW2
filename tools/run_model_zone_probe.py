"""Verify and execute the isolated exact-native Test40 model-zone probe.

Builds a fastfile with include/bo1_model.hpp and loads it with the retail x64
DB_LoadXFile and every XModel/Material/GfxImage loader under it (see
tools/model_zone_probe.cpp). Optionally also carves the Kino zombie from a
local decompressed zombie_theater zone (--bo1-theater, never committed).
"""
import argparse
import hashlib
import json
import platform
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
import pefile
from audit_actor_capacity import EXACT_SHA256

# First bytes of every native routine the probe runs unmodified...
EXECUTED = {
    0xdb7f0: '48 89 5c 24 08 55 56 57 48 83 ec 70',   # DB_LoadXFile
    0xf3d20: '48 89 0d f1 5a a7 00 48 8b 01 48 89',   # DB_InitStreams
    0xf3e60: '48 89 5c 24 08 57 48 83 ec 20 8b 1d',   # DB_PushStreamPos
    0xf3e20: '8b 05 02 5a a7 00 48 8d 15 03 5a a7',   # DB_PopStreamPos
    0xf3eb0: '40 53 48 83 ec 20 8b 05 54 59 a7 00',   # DB_SetStreamIndex (vertex/index copies)
    0xf3cf0: '48 63 c1 f7 d1 48 03 05 24 5b a7 00',   # DB_AllocStreamPos
    0xf3fc0: '84 c9 74 48 53 48 83 ec 20 49 63 d8',   # Load_Stream
    0xf40a0: '48 89 5c 24 08 57 48 83 ec 20 48 8b',   # Load_XString
    0xf4010: '48 89 5c 24 08 48 89 74 24 10 57 48',   # Load_TempString
    0xf4100: '48 8b 05 49 e7 58 00 44 0f b7 01 48',   # script string remap
    0xe2350: '48 83 ec 28 48 8b 15 1d fb 59 00 41',   # Load_ScriptStringList
    0xe22e0: '48 89 5c 24 08 57 48 83 ec 20 8b fa',   # Load_ScriptStringArray
    0xe6810: '48 83 ec 28 48 8b 15 25 c0 59 00 41',   # Load_XAsset
    0xe6850: '48 8b 05 e9 bf 59 00 8b 10 85 d2 75',   # Load_XAssetHeader
    0xe7120: '40 53 48 83 ec 20 48 8b 15 53 ae 59',   # Load_XModelPtr
    0xe6c80: '48 89 5c 24 08 57 48 83 ec 20 48 8b',   # Load_XModel
    0xe73a0: '40 53 48 83 ec 20 48 8b 15 2b a8 59',   # Load_XModelSurfsPtr
    0xe72e0: '48 83 ec 28 48 8b 15 e5 a8 59 00 41',   # Load_XModelSurfs
    0xe75d0: '48 89 5c 24 18 56 48 83 ec 20 44 6b',   # Load_XSurfaceArray
    0xe7900: '48 83 ec 28 48 8b 15 a5 ad 59 00 41',   # Load_XSurfaceVertexInfo
    0xe7850: '48 83 ec 28 48 8b 15 1d ae 59 00 41',   # Load_XSurfaceCollisionTree
    0xe0bf0: '48 89 5c 24 10 57 48 83 ec 20 8b fa',   # Load_MaterialHandleArray
    0xe0910: '48 83 ec 28 48 8b 15 45 07 5a 00 41',   # Load_Material
    0xe0cf0: '48 83 ec 28 48 8b 15 55 18 5a 00 41',   # Load_MaterialInfo
    0xe12c0: '40 53 48 83 ec 20 48 8b 15 8b fd 59',   # Load_MaterialTechniqueSetPtr
    0xe11a0: '48 89 5c 24 08 57 48 83 ec 20 48 8b',   # Load_MaterialTechniqueSet
    0xde270: '40 53 48 83 ec 20 48 8b 15 2b 46 5a',   # Load_GfxImagePtr
    0xde1c0: '48 83 ec 28 48 8b 15 d5 46 5a 00 41',   # Load_GfxImage
    0xde790: '40 53 48 83 ec 20 48 8b 15 db 40 5a',   # Load_GfxTexture
    0xf39a0: '48 8b 01 48 85 c0 74 1d 0f 10 40 14',   # SetModelSurfs
    0xf25a0: '0f b6 05 8d 6e a7 00 88 01 c3 cc cc',   # zone handle
    0xdc060: 'b9 03 00 00 00 e9 86 7c 01 00 cc cc',   # asset array allocation (align 4)
    0xe2090: '40 53 48 83 ec 20 48 8b 15 83 fe 59',   # Load_PhysPresetPtr
    0xe1ee0: '40 53 48 83 ec 20 48 8b 15 ab fc 59',   # Load_PhysCollmapPtr
    0xa650:  'c2 00 00 cc cc cc cc cc cc cc cc cc',   # image load action
}
# ...and of every routine it replaces (I/O, allocation, registration, upload).
REPLACED = {
    0xdbb40: '48 89 5c 24 08 57 48 83 ec 20 8b da',   # file reader start
    0xdbe50: '48 89 5c 24 08 48 89 6c 24 10 48 89',   # raw header read
    0xdba70: '48 8d 0d 31 66 25 00 e9 34 4c 1b 00',   # bad magic error
    0xdbae0: '48 83 ec 28 33 d2 48 8d 05 a3 65 25',   # inflateInit (unsigned only)
    0xdbbc0: '48 89 6c 24 10 48 89 74 24 18 48 89',   # inflated stream read
    0x1fb5f0: '48 89 54 24 10 4c 89 44 24 18 4c 89',  # version error
    0x2906b0: '48 89 4c 24 08 48 89 54 24 10 4c 89',  # Com_Error
    0xee1b0: '48 89 5c 24 08 48 89 74 24 10 57 48',   # DB_AllocXZoneMemory
    0xee450: '40 53 48 83 ec 20 48 83 b9 80 00 00',   # vertex/index buffer finish
    0xdbb30: '48 8d 0d b1 53 52 00 e9 f4 fb ff ff',   # inflateEnd
    0xdbaa0: '48 83 ec 28 83 3d ed 53 52 00 00 74',   # post-load
    0xf0920: '40 53 b8 30 70 00 00 e8 d4 89 1c 00',   # post-load
    0xf3810: '40 53 48 83 ec 30 48 8b 01 48 8b d9',   # DB_AddXAsset(xmodel)
    0xf38e0: '40 53 48 83 ec 30 48 8b 01 48 8b d9',   # DB_AddXAsset(xmodelsurfs)
    0xf2af0: '40 53 48 83 ec 30 48 8b 01 48 8b d9',   # DB_AddXAsset(material)
    0xf2c70: '40 57 48 83 ec 30 48 8b 01 48 8b f9',   # DB_AddXAsset(techset)
    0xf25b0: '40 53 48 83 ec 30 48 8b 01 48 8b d9',   # DB_AddXAsset(image)
    0x23db0: '40 53 57 48 83 ec 68 48 8b 05 e2 23',   # Load_Texture
    0x21a670: '48 c7 c0 ff ff ff ff 66 0f 1f 84 00',  # SL_GetString_
}
# DB_AddXAsset wrappers pass these asset types to the core (0xf0570).
WRAPPER_TYPES = {0xf3810: (0x65, 4), 0xf38e0: (0x65, 3), 0xf2af0: (0x65, 5), 0xf2c70: (0x65, 9), 0xf25b0: (0x65, 10)}


def check_native(pe):
    for table in (EXECUTED, REPLACED):
        for rva, expected in table.items():
            want = bytes.fromhex(expected)
            if pe.get_data(rva, len(want)) != want:
                raise ValueError(f'Native routine at {rva:#x} differs')
    for rva, (at, kind) in WRAPPER_TYPES.items():
        code = pe.get_data(rva + at, 5)
        if code[0] != 0xb9 or struct.unpack('<I', code[1:])[0] != kind:
            raise ValueError(f'DB_AddXAsset wrapper {rva:#x} no longer passes type {kind}')
        call = rva + at + 12
        if pe.get_data(call, 1) != b'\xe8' or call + 5 + struct.unpack('<i', pe.get_data(call + 1, 4))[0] != 0xf0570:
            raise ValueError(f'DB_AddXAsset wrapper {rva:#x} no longer calls the core')
    # Fastfile header: IWffu100 accepted, version 0x114, inflateInit_ with a zlib header.
    if pe.get_data(0xdb82a, 10) != bytes.fromhex('48 bd 49 57 66 66 75 31 30 30') or pe.get_data(0xdb85d, 7) != bytes.fromhex('41 81 f9 14 01 00 00'):
        raise ValueError('Fastfile magic/version check differs')
    if pe.get_data(0x2b3c46, 5) != bytes.fromhex('ba 0f 00 00 00'):
        raise ValueError('Zone inflate window bits differ')
    # Zone blocks are page aligned (alignment table 4096).
    if struct.unpack('<I', pe.get_data(0x3bf8f8, 4))[0] != 4096 or struct.unpack('<I', pe.get_data(0x3bf904, 4))[0] != 4096:
        raise ValueError('Zone block alignment differs')


def run(executable, theater=None, compiler='g++', cc='gcc', common=None):
    if platform.system() != 'Linux' or platform.machine().lower() not in ('amd64', 'x86_64'):
        raise ValueError('This probe requires Linux/x86-64')
    data = executable.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXACT_SHA256:
        raise ValueError('Exact executable SHA256 mismatch')
    pe = pefile.PE(data=data)
    check_native(pe)
    compiler_path, cc_path = shutil.which(compiler), shutil.which(cc)
    if not compiler_path or not cc_path:
        raise ValueError('Host compiler unavailable')
    tools = Path(__file__).resolve().parent
    root_dir = tools.parent
    with tempfile.TemporaryDirectory(prefix='bo1mw2-model-zone-') as directory:
        root = Path(directory)
        image = root / 'private-mapped-image.bin'
        program = root / 'model-zone-probe'
        image.write_bytes(pe.get_memory_mapped_image())
        subprocess.run([cc_path, '-c', '-O2', str(root_dir / 'third_party' / 'miniz' / 'miniz_tinfl.c'), '-o', str(root / 'tinfl.o')], check=True)
        subprocess.run([compiler_path, '-std=c++17', '-O2', '-I', str(root_dir / 'include'), str(tools / 'model_zone_probe.cpp'),
                        str(root / 'tinfl.o'), '-o', str(program)], check=True)
        args = [str(program), str(image)] + ([str(theater)] if theater else []) + ([str(common)] if theater and common else [])
        result = subprocess.run(args, capture_output=True, text=True, timeout=300)
        if result.returncode:
            raise ValueError(f'Native model-zone probe failed ({result.returncode}): {result.stderr.strip()}')
        report = json.loads(result.stdout)
        report['executable_sha256'] = EXACT_SHA256
        report['executed_routines'] = sorted(hex(r) for r in EXECUTED)
        report['replaced_routines'] = sorted(hex(r) for r in REPLACED)
        return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--bo1-theater', type=Path, help='local decompressed zombie_theater zone (optional)')
    parser.add_argument('--bo1-common', type=Path, help='local decompressed common_zombie zone (optional, Test45 powerups)')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = run(args.executable, args.bo1_theater, common=args.bo1_common)
    rendered = json.dumps(report, indent=2) + '\n'
    args.output.write_text(rendered)
    print(rendered, end='')
