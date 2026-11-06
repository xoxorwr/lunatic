#!/usr/bin/env python3
"""Read Linux process memory without attaching a debugger or changing the process."""
import argparse
from collections import defaultdict
from pathlib import Path
import re


def report(pid):
    root = Path('/proc') / str(pid)
    groups = defaultdict(lambda: defaultdict(int))
    totals = defaultdict(int)
    category = 'anonymous / heap (allocator owner unknown)'
    header = re.compile(r'^[0-9a-f]+-[0-9a-f]+\s+\S+\s+\S+\s+\S+\s+\d+\s*(.*)$')
    driver = re.compile(r'(lib(?:GL|EGL|GLX|glapi|gbm|drm|LLVM)|_dri\.so|/dri/)', re.I)
    executable = str((root / 'exe').resolve())
    for line in (root / 'smaps').read_text().splitlines():
        match = header.match(line)
        if match:
            path = match[1]
            if not path or path.startswith('['):
                category = 'anonymous / heap (allocator owner unknown)'
            elif driver.search(path):
                category = 'graphics driver file mappings'
            elif path == executable:
                category = 'launcher executable / bundled resources'
            else:
                category = 'other file mappings (libraries, fonts, etc.)'
            continue
        fields = line.split()
        if len(fields) == 3 and fields[2] == 'kB':
            key = fields[0].rstrip(':')
            value = int(fields[1])
            groups[category][key] += value
            totals[key] += value
    print(f'PID {pid}: {(root / "comm").read_text().strip()}')
    print('MiB (PSS apportions shared pages; private = clean + dirty)')
    print(f'{"Mapping category":49s} {"RSS":>9s} {"PSS":>9s} {"Private":>9s}')
    for name, values in [*sorted(groups.items()), ('TOTAL', totals)]:
        private = values['Private_Clean'] + values['Private_Dirty']
        print(f'{name:49s} {values["Rss"]/1024:9.2f} '
              f'{values["Pss"]/1024:9.2f} {private/1024:9.2f}')
    print(f'Swap: {totals["Swap"]/1024:.2f} MiB')
    print('Anonymous allocations may belong to GTK, the driver, or the launcher;')
    print('these mapping categories cannot distinguish their owners. GPU memory')
    print('and allocations in Xorg/the compositor are not fully represented here.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int, help='PID of the running launcher')
    args = parser.parse_args()
    if args.pid <= 0:
        parser.error('PID must be positive')
    try:
        report(args.pid)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Cannot read process {args.pid}: {error}\n')


if __name__ == '__main__':
    main()
