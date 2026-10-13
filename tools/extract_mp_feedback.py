"""Extract only MP hit feedback evidence from owner-supplied MW2 fastfiles.
Never commit decoded zones, complete scripts or complete fastfiles.
"""
import argparse, hashlib, json, mmap, struct, wave, zlib
from pathlib import Path

def decode_fastfile(source, destination):
    # IWffs100 authenticates 2 MiB payload groups with 8 KiB hash blocks.
    with source.open('rb') as inp, destination.open('wb') as out:
        header=inp.read(0x4015)
        if header[:8]!=b'IWff0100' or struct.unpack_from('<I',header,8)[0]!=276 or header[21:29]!=b'IWffs100':
            raise ValueError('Unsupported MW2 fastfile')
        stream=zlib.decompressobj(); total=0
        while not stream.eof:
            block=inp.read(0x200000)
            if not block: raise ValueError('Truncated compressed zone')
            pending=block
            while pending and not stream.eof:
                chunk=stream.decompress(pending,0x100000)
                total+=len(chunk)
                if total>0x20000000:raise ValueError('Zone exceeds extraction bound')
                out.write(chunk);pending=stream.unconsumed_tail
            if not stream.eof and len(inp.read(0x2000))!=0x2000:raise ValueError('Missing authentication block')
        out.write(stream.flush())
    return total

def extract(common, localized, output):
    output.mkdir(parents=True,exist_ok=True)
    report={'source_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (common,localized)}}
    with common.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as zone:
        name=b'maps/mp/gametypes/_damagefeedback.gsc\0'; at=zone.find(name)
        if at<16 or zone.find(name,at+1)>=0:raise ValueError('Feedback script is missing/ambiguous')
        packed,size=struct.unpack_from('<II',zone,at-16);begin=at+len(name)
        text=zlib.decompress(zone[begin:begin+packed])
        if len(text)!=size:raise ValueError('Script length mismatch')
        (output/'_damagefeedback.gsc').write_bytes(text)
        report['feedback_script_sha256']=hashlib.sha256(text).hexdigest()
    with localized.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as zone:
        alias=zone.find(b'MP_hit_alert\0');name=b'misc/mp_hit_indication_3c.wav\0';at=zone.find(name)
        if alias<0 or at<64 or zone.find(name,at+1)>=0:raise ValueError('MP sound missing/ambiguous')
        # LoadedSound's 64-byte record precedes its inline filename. Data length
        # is at +32, NOT the +24 file-size field. PCM follows four-byte padding.
        record=at-64;tag,channels,rate,average,align,bits=struct.unpack_from('<HHIIHH',zone,record+8)
        length=struct.unpack_from('<I',zone,record+32)[0]
        start=(at+len(name)+3)&~3
        # This zone's virtual alignment includes four extra zero bytes.
        if (tag,channels,rate,average,align,bits,length)!=(1,1,44100,88200,2,16,2968):
            raise ValueError('Unrecognized original hit-alert PCM layout')
        start+=4
        pcm=bytes(zone[start:start+length])
        if zone[start+length:start+length+12]!=b'\xff'*12:raise ValueError('PCM boundary is not the next asset')
        volume=struct.unpack_from('<f',zone,alias+65)[0]
        with wave.open(str(output/'MP_hit_alert.wav'),'wb') as wav:
            wav.setnchannels(channels);wav.setsampwidth(bits//8);wav.setframerate(rate);wav.writeframes(pcm)
        report['sound']={'alias':'MP_hit_alert','sample':name[:-1].decode(),'rate':rate,'channels':channels,'bits':bits,'pcm_bytes':length,'pcm_sha256':hashlib.sha256(pcm).hexdigest(),'volume':volume,'pitch':1.0}
    (output/'evidence.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('common_zone',type=Path);p.add_argument('localized_zone',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(extract(a.common_zone,a.localized_zone,a.output),indent=2))
