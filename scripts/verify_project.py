"""Check release contents without launching the game. Python 3.8+, standard library."""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import re
import sys
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hashes', action='store_true', help='verify original game file manifest')
    args = parser.parse_args()
    errors = []
    required = ['UsrAI.cpp', 'UsrAI.h', 'newAOE.pro', 'config.json', 'res.rcc',
                'map.njust', 'map1.njust', 'map2.njust', 'map3.njust',
                'LICENSE', 'licenses/framework-MIT.txt']
    for name in required:
        if not (ROOT / name).is_file():
            errors.append('Missing required file: ' + name)
    for name in ['config.json', 'map.njust', 'map1.njust', 'map2.njust', 'map3.njust']:
        try:
            json.loads((ROOT / name).read_text(encoding='utf-8-sig'))
        except (OSError, ValueError) as exc:
            errors.append('Invalid JSON ' + name + ': ' + str(exc))
    if (ROOT / 'res.rcc').is_file() and (ROOT / 'res.rcc').read_bytes()[:4] != b'qres':
        errors.append('res.rcc is not a Qt binary resource archive')
    try:
        pro = (ROOT / 'newAOE.pro').read_text(encoding='utf-8-sig')
        for name in re.findall(r'(?m)^\s*([\w/.-]+\.(?:cpp|h|hpp|ui))\s*(?:\\)?\s*$', pro):
            # Current target is Windows; matching casing is not required there.
            folder = ROOT / Path(name).parent
            if not folder.is_dir() or not any(p.name.casefold() == Path(name).name.casefold() for p in folder.iterdir()):
                errors.append('qmake references missing file: ' + name)
    except OSError as exc:
        errors.append(str(exc))
    for page in ROOT.rglob('*.md'):
        if any(part in {'.git', '.build', 'build', 'private'} for part in page.relative_to(ROOT).parts):
            continue
        body = page.read_text(encoding='utf-8-sig')
        for link in re.findall(r'\]\(([^)]+)\)', body):
            dest = link.strip().split(' "')[0].strip('<>')
            if re.match(r'^[a-zA-Z][\w+.-]*:', dest) or dest.startswith('#'):
                continue
            dest = unquote(dest.split('#')[0])
            if dest and not (page.parent / dest).exists():
                errors.append('Broken link in ' + str(page.relative_to(ROOT)) + ': ' + dest)
    try:
        with (ROOT / 'data/results.csv').open(encoding='utf-8', newline='') as stream:
            rows = list(csv.DictReader(stream))
        wins = [row for row in rows if row['outcome'] == 'win']
        print('Historical samples: {}, wins: {}, best winning frames: {}'.format(
            len(rows), len(wins), min(int(row['frame']) for row in wins)))
    except (OSError, ValueError, KeyError) as exc:
        errors.append('Invalid results data: ' + str(exc))
    if args.hashes:
        try:
            manifest = json.loads((ROOT / 'data/source-sha256.json').read_text(encoding='utf-8'))
            for name, expected in manifest.items():
                path = ROOT / name
                if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                    errors.append('Original game file differs: ' + name)
        except (OSError, ValueError) as exc:
            errors.append('Cannot verify manifest: ' + str(exc))
    for error in errors:
        print('ERROR:', error, file=sys.stderr)
    if errors:
        return 1
    print('Project structure, runtime data and Markdown links verified.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
