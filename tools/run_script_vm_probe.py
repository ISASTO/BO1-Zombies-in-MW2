"""Reproduce stock child-variable exhaustion and verify the 8-pool expansion.

The fixture maps the exact executable with the owned script region at its
synthetic RVA, applies the generated patches with their fixups, and runs the
native script-variable routines directly.
"""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
import pefile
from audit_actor_capacity import EXACT_SHA256
from prepare_script_vm import prepare,check_capacity_disjoint,check_mod_patterns,REGION,REGION_BYTES,LIST_GUARD,DOBJ_GUARD,LIST,PARENT_END,OLD_PARENT_END,MAPS,OLD_MAPS,MAP_BYTES,OLD_MAP_BYTES,CHILD_BASE,OLD_CHILD_BASE

HERE=Path(__file__).resolve().parent

def fixture(pe,rows):
    image=bytearray(pe.get_memory_mapped_image())
    image.extend(bytes(REGION+REGION_BYTES-len(image)))
    for r in rows:
        at=r['rva'];after=bytearray(bytes.fromhex(r['after']))
        if r['before'] and bytes(image[at:at+len(after)])!=bytes.fromhex(r['before']):raise ValueError(f'Original bytes changed at {at:x}')
        for off,width,target,end,relative in r['fixups']:
            value=target-end              # identity mapping: relative or image-relative to end
            after[off:off+width]=value.to_bytes(width,'little',signed=True)
        image[at:at+len(after)]=after
    return bytes(image)

def run(executable):
    raw=executable.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXACT_SHA256:raise ValueError('Unsupported executable')
    pe=pefile.PE(data=raw,fast_load=True);rows,meta=prepare(pe,raw)
    meta['capacity_rows_checked_disjoint']=check_capacity_disjoint(rows)
    meta['runtime_signatures_unchanged']=check_mod_patterns(pe,rows)
    meta['native_patch_rows']=sum(1 for r in rows if r['rva']<REGION)
    results={}
    with tempfile.TemporaryDirectory(prefix='bo1mw2-script-vm-') as d:
        root=Path(d);program=root/'probe'
        subprocess.run(['g++','-std=c++17','-O2','-DSCRIPT_VM_HEADER','-I',str(HERE.parent/'include'),str(HERE/'script_vm_probe.cpp'),'-o',str(program)],check=True)
        for mode,patches in (('stock',[]),('expanded',rows),('header',None)):
            path=root/f'{mode}.bin'
            if patches is None:
                image=bytearray(pe.get_memory_mapped_image());image.extend(bytes(0x5001000-len(image)));path.write_bytes(bytes(image))
            else:path.write_bytes(fixture(pe,patches))
            p=subprocess.run([str(program),str(path),mode,hex(LIST_GUARD),hex(DOBJ_GUARD),hex(LIST),hex(OLD_PARENT_END if mode=='stock' else PARENT_END),hex(OLD_MAPS[1] if mode=='stock' else MAPS[1]),hex(OLD_MAP_BYTES if mode=='stock' else MAP_BYTES),hex(OLD_CHILD_BASE if mode=='stock' else CHILD_BASE)],capture_output=True,text=True,timeout=600)
            if p.returncode:raise ValueError(f'{mode} script VM probe failed ({p.returncode}): '+p.stderr)
            results[mode]=json.loads(p.stdout)
    s,e=results['stock'],results['expanded']
    h=results['header']
    if {k:v for k,v in h.items() if k!='mode'}!={k:v for k,v in e.items() if k!='mode'}:raise ValueError('Generated C++ header behaves differently from the manifest fixture')
    if 'child' not in s['error'] or 'child' not in e['error']:raise ValueError('Exhaustion was not reported by the native allocator')
    if s['lookup_misses'] or e['lookup_misses']:raise ValueError('Native lookup lost a child')
    if e['children_before_error']<3.9*s['children_before_error']:raise ValueError('Expanded capacity below expectation')
    if s['marked']!=s['mark_chain_objects'] or e['marked']!=e['mark_chain_objects'] or e['mark_highest_id']<0xf000:raise ValueError('Savegame mark incomplete')
    for name,r in (('stock',s),('expanded',e)):
        if r['object_keys_referenced']!=2 or r['object_keys_kept_alive']!=2 or r['object_keys_released']!=2 or not r['object_key_marked_for_save'] or not r['array_index_boundary']:
            raise ValueError(f'{name}: object-keyed children lose references: '+json.dumps({k:v for k,v in r.items() if k.startswith(('object_key','array_index','lowest'))}))
    if s['saved_object_numbers']!={'4660':'restored','33059':'fault'}:raise ValueError('Stock saved-object-number decode did not reproduce: '+json.dumps(s['saved_object_numbers']))
    if set(e['saved_object_numbers'].values())!={'restored'} or len(e['saved_object_numbers'])!=3:raise ValueError('Saved object numbers not restored: '+json.dumps(e['saved_object_numbers']))
    if s['entity_handles_before_error']!=0xfff or 'ENT_HANDLE_COUNT' not in s['entity_handle_error']:raise ValueError('Stock entity handle exhaustion did not reproduce: '+json.dumps({k:v for k,v in s.items() if k.startswith('entity_handle')}))
    if e['entity_handles_before_error']!=0x7fff or e['entity_handles_linked']!=0x7fff or e['entity_handles_cleared']!=e['entity_handles_before_error'] or e['entity_handles_refill']!=e['entity_handles_before_error']:
        raise ValueError('Entity handle pool not expanded or not released: '+json.dumps({k:v for k,v in e.items() if k.startswith('entity_handle')}))
    if e['object_key_high']<0xf000 or e['object_key_marked_id']<0xf000:raise ValueError('Object-key test did not reach high parent ids')
    if s['parent_objects_before_error']>36870 or e['parent_objects_before_error']<65000:raise ValueError('Parent space not expanded')
    if not e.get('mt_routing') or e.get('checkpoint_slot_bytes')!=0x2000000 or e.get('single_pool5_children')!=0xc7ff:raise ValueError('Early capacity checks incomplete')
    return {'executable_sha256':EXACT_SHA256,'metadata':meta,'stock':s,'expanded':e,'header_relocated_region':h,
            'capacity_ratio':round(e['children_before_error']/s['children_before_error'],3),
            'returned_error':'Test36: exceeded maximum number of child1/parent script variables and VM thread-resume crashes at 0x227ddc with pools at most 25% used',
            'substitutions':['native error reporters 0x2906b0 and 0x1fb5f0 record the message and unwind'],
            'scope':'Exact native InitVariables, AllocObject, GetNewVariable, FindArrayVariable, RemoveVariable, RemoveRefToObject, MT_Init, MT_Alloc, MT_AllocIndex, MT_Free and checkpoint slot init on Linux with guard pages after the relocated list and dobj records. No Windows mission or real level scripts.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('executable',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    r=run(a.executable);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:r[k] for k in ('stock','expanded','capacity_ratio')},indent=2))
