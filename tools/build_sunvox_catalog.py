"""Embed the pinned SunVox effect bank with stable filenames and author credits."""
import json
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:])
patches = sorted(root.rglob('*.sunsynth'))
if len(patches) != 235:
    raise SystemExit('Expected the pinned 235-effect SunVox bank')
with output.open('w', encoding='utf-8') as dest:
    entries = []
    for i, path in enumerate(patches):
        data = path.read_bytes()
        dest.write('static const unsigned char patch_%d[] = {\n' % i)
        for at in range(0, len(data), 32):
            dest.write(','.join(str(b) for b in data[at:at+32]) + ',\n')
        dest.write('};\n')
        entries.append('{%s,%s,patch_%d,sizeof(patch_%d)}' %
                       (json.dumps(path.stem), json.dumps(path.parent.name), i, i))
    dest.write('static const TsSunVoxFactory factories[] = {\n' + ',\n'.join(entries) + '\n};\n')
