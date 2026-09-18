#!/usr/bin/env python3
"""Extract files from Enfusion (Arma Reforger) .pak archives.
Usage: python unpak.py <out_dir> <file.pak> [more.pak ...] [--filter .c,.conf,.et,.layout]
Only the directory index (at the end of the pak) is read fully; file data is read by seek, so
multi-GB paks are handled quickly. Defaults to extracting only script files (*.c)."""
import struct, os, sys, zlib

def extract(pak, out_dir, exts):
    with open(pak, 'rb') as fh:
        hdr = fh.read(12)
        if hdr[:4] != b'FORM' or hdr[8:12] != b'PAC1':
            print('Not a PAC1 pak:', pak); return 0
        end = 8 + struct.unpack('>I', hdr[4:8])[0]
        pos = 12
        idx = None
        while pos < end:
            fh.seek(pos); tag = fh.read(4); size = struct.unpack('>I', fh.read(4))[0]
            if tag == b'FILE':
                idx = fh.read(size); break
            pos += 8 + size
        if idx is None:
            print('No FILE index in', pak); return 0
        entries = []; p = [0]
        def parse(path):
            t = idx[p[0]]; nl = idx[p[0]+1]; name = idx[p[0]+2:p[0]+2+nl].decode('utf-8', 'replace'); p[0] += 2 + nl
            if t == 0:
                cnt = struct.unpack('<I', idx[p[0]:p[0]+4])[0]; p[0] += 4
                for _ in range(cnt): parse(path + name + '/' if name else path)
            else:
                off, sz, osz = struct.unpack('<III', idx[p[0]:p[0]+12]); p[0] += 24
                entries.append((path + name, off, sz, osz))
        parse('')
        n = 0
        for rel, off, sz, osz in entries:
            if exts and not any(rel.lower().endswith(e) for e in exts):
                continue
            fh.seek(off); data = fh.read(sz)
            if sz != osz:
                try: data = zlib.decompress(data)
                except zlib.error:
                    try: data = zlib.decompress(data, -15)
                    except zlib.error as e:
                        print('decompress failed:', rel, e); continue
            outp = os.path.join(out_dir, rel)
            os.makedirs(os.path.dirname(outp), exist_ok=True)
            with open(outp, 'wb') as o: o.write(data)
            n += 1
        print(f'{pak}: {len(entries)} entries, extracted {n}')
        return n

if __name__ == '__main__':
    args = sys.argv[1:]
    exts = ['.c']
    if '--filter' in args:
        i = args.index('--filter'); exts = [e.strip().lower() for e in args[i+1].split(',')]; del args[i:i+2]
    if len(args) < 2:
        # No arguments (e.g. run from an IDE): use defaults for Rick's setup.
        import glob
        here = os.path.dirname(os.path.abspath(__file__))
        out_dir = os.path.join(here, '..', '_ref', 'vanilla')
        paks = sorted(glob.glob(r'D:\SteamLibrary\steamapps\common\Arma Reforger\addons\data\data*.pak'))
        if not paks:
            print(__doc__); print('No paks found at default location.'); sys.exit(1)
        print('No arguments given; using defaults ->', os.path.abspath(out_dir))
    else:
        out_dir, paks = args[0], args[1:]
    total = 0
    for pk in paks: total += extract(pk, out_dir, exts)
    print('total extracted:', total)