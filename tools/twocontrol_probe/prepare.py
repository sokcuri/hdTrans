import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile

root = pathlib.Path(sys.argv[1]).resolve()
root.mkdir(exist_ok=False)
evidence = root / 'evidence'
evidence.mkdir()
records = []


def download(name, url, expected):
    folder = root / (name + '-download')
    folder.mkdir()
    archive = folder / name
    with urllib.request.urlopen(url) as response, archive.open('wb') as output:
        shutil.copyfileobj(response, output)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != expected:
        raise RuntimeError('SHA256 mismatch: ' + name)
    records.append({'file': name, 'url': url, 'sha256': digest})
    return archive


def safe_path(name):
    normalized = name.replace('\\', '/')
    path = pathlib.PurePosixPath(normalized)
    if (not normalized or normalized.startswith('/') or ':' in normalized
            or any(part in ('..', '.') for part in normalized.split('/') if part)
            or re.search(r'[\x00-\x1f]', normalized)):
        raise RuntimeError('Unsafe archive path')
    return path


rar = download('TwoControl.rar',
    'https://github.com/twocomet/TwoControl/releases/download/Releases/TwoControl.rar',
    '7faf4f6c7698da1008e0fe67b9e293e55fbc97117b9e54018301f0cb683dc232')
release = download('hdTrans-0.1.1-windows.zip',
    'https://github.com/sokcuri/hdTrans/releases/download/v0.1.1/hdTrans-0.1.1-windows.zip',
    '347c72a82c78449492205e13da477d90590ba03e474fdb9c05c573e1ce3cb8b8')
sums = download('SHA256SUMS.txt',
    'https://github.com/sokcuri/hdTrans/releases/download/v0.1.1/SHA256SUMS.txt',
    'dadd6db1a6354bddbe453f16ce4b990965e16dce6d23ff618c540365e6054fcd')
if ('347c72a82c78449492205e13da477d90590ba03e474fdb9c05c573e1ce3cb8b8  '
        'hdTrans-0.1.1-windows.zip') not in sums.read_text():
    raise RuntimeError('Published checksum missing')

seven = shutil.which('7z') or str(pathlib.Path('C:/Program Files/7-Zip/7z.exe'))
listing = subprocess.check_output([seven, 'l', '-slt', '-sccUTF-8', str(rar)], encoding='utf-8')
entries = listing.split('----------', 1)[1].strip().split('\n\n')
seen = set()
for entry in entries:
    fields = dict(line.split(' = ', 1) for line in entry.splitlines() if ' = ' in line)
    if 'Path' not in fields:
        continue
    path = safe_path(fields['Path'])
    folded = str(path).casefold()
    if folded in seen:
        raise RuntimeError('Duplicate archive path')
    seen.add(folded)
    if any('link' in key.lower() for key in fields) or 'L' in fields.get('Attributes', ''):
        raise RuntimeError('Archive link rejected')
    if fields.get('Encrypted', '-') != '-':
        raise RuntimeError('Encrypted archive entry rejected')
app = root / 'application'
app.mkdir()
subprocess.run([seven, 'x', '-y', '-bd', '-o' + str(app), str(rar)], check=True,
               stdout=subprocess.DEVNULL)
for path in app.rglob('*'):
    if path.is_symlink() or not path.resolve().is_relative_to(app):
        raise RuntimeError('Extracted link rejected')
package = root / 'release'
package.mkdir()
with zipfile.ZipFile(release) as archive:
    for info in archive.infolist():
        safe_path(info.filename)
        if (info.external_attr >> 16) & 0o170000 == 0o120000:
            raise RuntimeError('ZIP link rejected')
    archive.extractall(package)

engines = list(package.rglob('J2KEngine.dll'))
if len(engines) != 1:
    raise RuntimeError('Expected one released x86 engine: ' + str([str(p.relative_to(package)) for p in engines]))
engine = engines[0]
pe = engine.read_bytes()
pe_offset = int.from_bytes(pe[0x3c:0x40], 'little')
if pe[pe_offset:pe_offset + 6] != b'PE\0\0\x4c\x01':
    raise RuntimeError('Released J2K engine is not x86 PE')
data = engine.parent / 'hdTrans'
if not data.is_dir():
    raise RuntimeError('Shipped hdTrans directory missing')
backup = root / 'stock-engine-backup'
backup.mkdir()
for name in ('Ehnd.dll', 'Ehnd.dlx'):
    target = app / 'Ehnd' / name
    shutil.copy2(target, backup / name)
    shutil.copy2(engine, target)
shutil.copytree(data, app / 'Ehnd' / 'hdTrans')
plugin = app / 'dll' / 'CustomData' / 'ezTransXP.dll'
plugin_sha = hashlib.sha256(plugin.read_bytes()).hexdigest()
if plugin_sha != '476638388433dab7c9ac83947d1f7190773b924dc973489b2cbf6f6513a42734':
    raise RuntimeError('Unexpected actual plugin')
metadata = {'downloads': records, 'archive_entries_validated': len(seen),
            'plugin_sha256': plugin_sha,
            'replacement_sha256': hashlib.sha256(engine.read_bytes()).hexdigest(),
            'layout': 'All stock files retained except backed-up Ehnd.dll and Ehnd.dlx; both replaced with released x86 J2KEngine.dll; shipped hdTrans directory copied beside them.'}
(evidence / 'preparation.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
print(json.dumps(metadata, indent=2))
