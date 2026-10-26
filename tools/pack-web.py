#!/usr/bin/env python3
"""Generate immutable, gzip-compressed assets for the embedded HTTP server."""
import gzip
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = root / 'web/dist'
target = root / 'firmware/components/http_api/generated'
target.mkdir(parents=True, exist_ok=True)
entries = []
manifest = []
for index, path in enumerate(sorted(source.rglob('*'))):
    if not path.is_file():
        continue
    raw = path.read_bytes()
    data = gzip.compress(raw, compresslevel=9, mtime=0)
    name = f'asset_{index}'
    url = '/' + path.relative_to(source).as_posix()
    content_type = {'.html':'text/html; charset=utf-8', '.js':'text/javascript', '.css':'text/css', '.svg':'image/svg+xml', '.png':'image/png'}.get(path.suffix,'application/octet-stream')
    manifest.append({'url':url,'bytes':len(raw),'gzip_bytes':len(data),'sha256':hashlib.sha256(raw).hexdigest()})
    entries.append((name, url, content_type, data))
if not any(url=='/index.html' for _,url,_,_ in entries):
    raise SystemExit('Build web/ first; index.html missing')
with (target/'web_assets.h').open('w') as out:
    out.write('#pragma once\n#include <stddef.h>\nstruct WebAsset { const char *path; const char *type; const unsigned char *data; size_t size; };\n')
    for name, _, _, data in entries:
        out.write(f'static const unsigned char {name}[] = {{'+','.join(str(b) for b in data)+'};\n')
    out.write('static const WebAsset web_assets[] = {\n')
    for name, url, content_type, data in entries:
        out.write('{'+json.dumps(url)+','+json.dumps(content_type)+f',{name},sizeof({name})'+'},\n')
    out.write('};\n')
(target/'web_assets.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(f'Packed {len(entries)} assets: {sum(x["bytes"] for x in manifest)} raw / {sum(x["gzip_bytes"] for x in manifest)} gzip bytes')
