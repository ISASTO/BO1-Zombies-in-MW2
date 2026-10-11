"""Produce a derived BO1 dependency/asset-reference manifest from owned local scripts.

Input: extract_bo1_scripts.py output, with zones explicitly ordered base to patch.
Output: hashes, paths, function line ranges and references, never script bodies.
This lexical audit is not a GSC compiler: dynamic references and runtime callbacks
require further inspection. Missing entries mean absent from this input set.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path, PurePosixPath

DEFAULT_ROOTS = [
    'maps/_zombiemode.gsc', 'maps/_zombiemode_spawner.gsc',
    'maps/_zombiemode_zone_manager.gsc', 'animscripts/zombie_init.gsc',
    'animscripts/zombie_combat.gsc', 'animscripts/zombie_melee.gsc',
    'animscripts/zombie_move.gsc', 'animscripts/zombie_run.gsc',
    'animscripts/zombie_pain.gsc', 'animscripts/zombie_death.gsc',
]
TOKENS = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/|/\#.*?\#/', re.S)
FUNCTION = re.compile(r'(?m)^\s*([A-Za-z_]\w*)\s*\([^{};]*?\)\s*\{')


def safe_script(name):
    name=name.replace('\\','/')
    path=PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or ':' in name or path.suffix.lower() not in ('.gsc','.csc'):
        raise ValueError(f'Unsafe or unsupported script path: {name}')
    return str(path)


def masked(text, strings=False):
    def replace(match):
        value=match[0]
        if value.startswith('"') and not strings:
            return value
        return ''.join('\n' if c=='\n' else ' ' for c in value)
    return TOKENS.sub(replace,text)


def inspect_script(name, data):
    name=safe_script(name)
    encoding='utf-8-sig'
    try:
        text=data.decode(encoding)
    except UnicodeDecodeError:
        # Original inputs contain non-UTF8 comment punctuation. Preserve every
        # byte/code-point position rather than lossy replacement or source edits.
        encoding='latin-1-byte-preserving'
        text=data.decode('latin-1')
    comments=masked(text)
    code=masked(text,strings=True)
    extension=PurePosixPath(name).suffix.lower()
    def dependency(value):
        return safe_script(value+extension)
    includes=sorted({dependency(value) for value in re.findall(r'(?mi)^\s*#include\s+([\w/\\]+)\s*;',comments)})
    calls=sorted({dependency(value) for value in re.findall(r'([A-Za-z_]\w*(?:[\\/][A-Za-z_]\w*)+)\s*::',code)})
    functions=[]
    for match in FUNCTION.finditer(code):
        if match[1].lower() in ('if','while','for','switch'):
            continue
        depth=1
        end=match.end()
        while end<len(code) and depth:
            if code[end]=='{': depth+=1
            elif code[end]=='}': depth-=1
            end+=1
        if depth:
            raise ValueError(f'{name}: unmatched function braces')
        functions.append(dict(name=match[1],start_line=code.count('\n',0,match.start(1))+1,
                              end_line=code.count('\n',0,end)+1))
    def literal_calls(pattern):
        values=sorted(set(re.findall(r'(?i)\b(?:'+pattern+r')\s*\(\s*"([^"\n]+)"',comments)))
        dynamic=len(re.findall(r'(?i)\b(?:'+pattern+r')\s*\(',code))-len(re.findall(r'(?i)\b(?:'+pattern+r')\s*\(\s*"([^"\n]+)"',comments))
        return values,dynamic
    aliases,dynamic_sound=literal_calls('playsound|playsoundtoplayer|playsoundatposition|precachealias')
    models,dynamic_models=literal_calls('setmodel|precachemodel')
    return dict(path=name,sha256=hashlib.sha256(data).hexdigest(),size_bytes=len(data),lexical_encoding=encoding,
        includes=includes,script_calls=calls,functions=functions,
        animtrees=sorted(set(re.findall(r'(?mi)^\s*#using_animtree\s*\(\s*"([^"\n]+)"',comments))),
        animation_symbols=sorted(set(re.findall(r'(?<![\w])%([A-Za-z_]\w*)',code))),
        literal_sound_aliases=aliases,dynamic_sound_calls=dynamic_sound,
        literal_models=models,dynamic_model_calls=dynamic_models)


def audit(root,zones,roots):
    resolved={}
    overridden=set()
    for zone in zones:
        if PurePosixPath(zone).name!=zone or zone in ('.','..'):
            raise ValueError('Zone must be a directory name, not a path')
        directory=root/zone
        if not directory.is_dir():
            raise ValueError(f'Zone directory is missing: {zone}')
        for path in sorted(directory.rglob('*')):
            if not path.is_file() or path.suffix.lower() not in ('.gsc','.csc'):
                continue
            path.resolve().relative_to(directory.resolve())
            name=safe_script(path.relative_to(directory).as_posix())
            key=name.casefold()
            if key in resolved: overridden.add(key)
            resolved[key]=(zone,path,name)
    pending=[safe_script(name).casefold() for name in roots]
    visited=set()
    missing=set()
    scripts=[]
    while pending:
        key=pending.pop()
        if key in visited: continue
        visited.add(key)
        if key not in resolved:
            missing.add(key); continue
        zone,path,name=resolved[key]
        item=inspect_script(name,path.read_bytes())
        item['zone']=zone
        scripts.append(item)
        pending.extend(dep.casefold() for dep in item['includes']+item['script_calls'])
    return dict(schema_version=1,zone_precedence=zones,roots=[safe_script(name) for name in roots],
        resolved_scripts=sorted(scripts,key=lambda item:item['path'].casefold()),
        missing_dependencies=sorted(missing),overridden_paths=sorted(overridden),
        limits=['Lexical script references only; dynamic calls/assets are not resolved.',
                'No native API, final map configuration or asset binary compatibility is inferred.',
                'Debug-only /# #/ blocks are excluded; missing paths are relative to the supplied input set.'])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path)
    parser.add_argument('--zones',nargs='+',required=True,help='Directory names, in base-to-patch order')
    parser.add_argument('--roots',nargs='+',default=DEFAULT_ROOTS)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    report=audit(args.input,args.zones,args.roots)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(f"{len(report['resolved_scripts'])} reachable scripts; {len(report['missing_dependencies'])} missing dependencies; no source bodies emitted")
