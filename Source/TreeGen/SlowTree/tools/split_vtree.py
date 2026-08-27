"""Splits a multi-plant .vtree into one standalone .vtree per Trunk root.

Sample.vtree (upstream) packs several authored plants into one project, laid out side by side via
TrunkParams posX/posZ. To study or adopt one, it has to come out on its own with the layout offset
zeroed.

Usage:
    python Source/TreeGen/SlowTree/tools/split_vtree.py <in.vtree> <outdir>
"""
import os
import re
import sys

# NodeType enum order from SlowTreeTypes.h — do not reorder, it is the serialised value.
TYPE_NAMES = ['Trunk', 'Roots', 'Branch', 'Twig', 'LeafCluster', 'Spine', 'Frond', 'Export',
              'Custom', 'ImportTrunk', 'ImportLeaf', 'Scatter']


def parse(text):
    """-> (header, {id: (type, body_lines)}, [(parent, child)])"""
    header = text.splitlines()[0]
    nodes = {}
    for m in re.finditer(r'^NODE (\d+) (\d+)([^\n]*)\n(.*?)^ENDNODE', text, re.S | re.M):
        nid, ntype, rest, body = int(m.group(1)), int(m.group(2)), m.group(3), m.group(4)
        nodes[nid] = (ntype, rest, body.splitlines())
    links = [(int(a), int(b)) for a, b in re.findall(r'^LINK (\d+) (\d+)', text, re.M)]
    return header, nodes, links


def descendants(root, links):
    """All nodes reachable from root, root included."""
    seen, stack = {root}, [root]
    while stack:
        cur = stack.pop()
        for a, b in links:
            if a == cur and b not in seen:
                seen.add(b)
                stack.append(b)
    return seen


def emit(header, nodes, links, keep):
    out = [header]
    for nid in sorted(keep):
        ntype, rest, body = nodes[nid]
        lines = []
        for line in body:
            # Zero the project layout offset; a standalone plant sits at the origin.
            if line.startswith('posX ') or line.startswith('posZ '):
                lines.append(line.split()[0] + ' 0')
            else:
                lines.append(line)
        out.append(f'NODE {nid} {ntype}{rest}')
        out.extend(lines)
        out.append('ENDNODE')
    for a, b in links:
        if a in keep and b in keep:
            out.append(f'LINK {a} {b}')
    return '\n'.join(out) + '\n'


def describe(nodes, links, keep):
    """One-line shape summary: the chain of levels with their counts."""
    COUNT_KEYS = ('branchCount', 'twigCount', 'leafCount', 'spineCount', 'rootCount')
    parts = []
    for nid in sorted(keep):
        ntype, _, body = nodes[nid]
        count = ''
        for line in body:
            key = line.split()[0] if line.split() else ''
            if key in COUNT_KEYS:
                count = f'({line.split()[1]})'
        parts.append(f'{TYPE_NAMES[ntype]}{count}')
    return ' + '.join(parts)


def main():
    src, outdir = sys.argv[1], sys.argv[2]
    with open(src, encoding='utf-8', errors='replace') as f:
        text = f.read()
    header, nodes, links = parse(text)
    os.makedirs(outdir, exist_ok=True)

    children = {b for _, b in links}
    roots = [nid for nid, (t, _, _) in nodes.items() if t == 0]
    orphans = [nid for nid in nodes if nid not in children and nid not in roots]

    for i, root in enumerate(sorted(roots)):
        keep = descendants(root, links)
        name = f'plant{i + 1}_trunk{root}'
        path = os.path.join(outdir, name + '.vtree')
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(emit(header, nodes, links, keep))
        print(f'{name}: {describe(nodes, links, keep)}')

    if orphans:
        print('unlinked nodes (not part of any plant): ' +
              ', '.join(f'{n}={TYPE_NAMES[nodes[n][0]]}' for n in sorted(orphans)))


if __name__ == '__main__':
    main()
