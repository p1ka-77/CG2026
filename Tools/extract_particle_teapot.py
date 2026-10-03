"""Extract the existing room's teapot (body, lid, handle), excluding its pitcher.
Run once when regenerating Models/particle_teapot.obj; Python is not needed to run the app.
The geometry keeps the attribution/license of breakfast_room.obj.
"""
from pathlib import Path
from collections import defaultdict

root = Path(__file__).resolve().parents[1]
positions, normals, faces = [], [], []
active = False
for line in (root / 'breakfast_room.obj').open():
    s = line.split()
    if not s:
        continue
    if s[0] == 'o':
        active = s[1].startswith('Teapot_and_Water_Pitcher')
    elif s[0] == 'v':
        positions.append(tuple(map(float, s[1:4])))
    elif s[0] == 'vn':
        normals.append(tuple(map(float, s[1:4])))
    elif s[0] == 'f' and active:
        faces.append([(int(t.split('/')[0])-1, int(t.split('/')[2])-1) for t in s[1:]])

parent = {i: i for f in faces for i, n in f}
def find(i):
    while parent[i] != i:
        parent[i] = parent[parent[i]]
        i = parent[i]
    return i
weld = {}
for i in parent:
    key = tuple(round(v, 5) for v in positions[i])
    if key in weld:
        parent[find(i)] = find(weld[key])
    else:
        weld[key] = i
for f in faces:
    for i, n in f[1:]:
        parent[find(i)] = find(f[0][0])
groups = defaultdict(list)
for f in faces:
    groups[find(f[0][0])].append(f)
# The pitcher is the tall component (top y=1.922); the three teapot parts top out at 1.396.
selected = [f for fs in groups.values()
            if max(positions[i][1] for f in fs for i, n in f) < 1.5 for f in fs]
used = sorted({v for f in selected for v in f})
lo = [min(positions[i][a] for i,n in used) for a in range(3)]
hi = [max(positions[i][a] for i,n in used) for a in range(3)]
center = [(lo[a]+hi[a])*0.5 for a in range(3)]
extent = max(hi[a]-lo[a] for a in range(3))
lines = ['# Extracted from breakfast_room.obj; see ../license.txt', 'o ParticleTeapot']
for i,n in used:
    lines.append('v ' + ' '.join(f'{(positions[i][a]-center[a])/extent:.7f}' for a in range(3)))
for i,n in used:
    lines.append('vn ' + ' '.join(map(str,normals[n])))
mapping = {v:i+1 for i,v in enumerate(used)}
lines.append('usemtl ParticleCeramic')
for f in selected:
    for j in range(1,len(f)-1):
        lines.append('f ' + ' '.join(f'{mapping[v]}//{mapping[v]}' for v in (f[0],f[j],f[j+1])))
out = root / 'Models' / 'particle_teapot.obj'
out.parent.mkdir(exist_ok=True)
out.write_text('\n'.join(lines)+'\n', encoding='ascii')
print(f'{out}: {len(used)} vertices, {sum(len(f)-2 for f in selected)} triangles')
