#!/usr/bin/env python3
"""Verify the vendored launcher against the original GitLab Git blob IDs."""
import hashlib
import json
import pathlib
root=pathlib.Path(__file__).resolve().parent.parent/'upstream'
manifest=json.loads((root/'manifest.json').read_text())
count=0
for entry in manifest['files']:
 if entry['type']!='blob':continue
 path=root/entry['path'];data=path.read_bytes()
 digest=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
 if digest!=entry['id']:raise SystemExit(f'Upstream integrity failure: {entry["path"]}')
 count+=1
print(f'Verified {count} original upstream files at {manifest["commit"]}.')
