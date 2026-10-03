"""Run with Blender --background --factory-startup --python this_file.
Uses the unmodified A.N.T. Landscape generator, not a substitute noise function.
"""
from pathlib import Path
import sys, json, hashlib
import bpy
import numpy as np

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'Tools' / 'vendor'))
import ant_noise

out = root / 'Assets' / 'Terrain'
out.mkdir(parents=True, exist_ok=True)
size, world, height = 1025, 2048.0, 320.0
# Property ordering is documented by ant_noise.noise_gen, lines 482 onward.
p = [0] * 65
p[0] = 'TerrainLab_ANT_Ridged'
p[10] = p[11] = p[12] = 12.0
p[13] = 17
p[17] = p[18] = p[19] = 1.0
p[20] = 1.6
p[21], p[22], p[23] = 'ridged_multi_fractal', 'PERLIN_ORIGINAL', 'PERLIN_ORIGINAL'
p[24], p[26], p[27], p[28] = 1.0, 7, 0.5, 2.0
p[29], p[30], p[31], p[32] = 1.05, 2.0, 1.0, 2.0
p[36], p[39], p[40] = 1.0, 10.0, -10.0
p[43] = p[44] = 4.0
p[53] = '0'
h = np.empty((size,size), dtype=np.float32)
for z in range(size):
    for x in range(size):
        h[z,x] = ant_noise.noise_gen((12.0*x/(size-1)-6, 12.0*z/(size-1)-6, 0.0), p)
    if z % 128 == 0:
        print(f'A.N.T. Landscape row {z}/{size}', flush=True)
# One normalization for the WHOLE map, never per tile (that would break edges).
h = (h - h.min()) / (h.max() - h.min())
raw = np.rint(h*65535).astype('<u2')
raw.tofile(out / 'heightmap.r16')
manifest = {
    'generator': 'Blender A.N.T. Landscape / ant_noise.noise_gen',
    'blender': bpy.app.version_string, 'source': 'https://github.com/blender/blender-addons/tree/main/ant_landscape',
    'generator_sha256': hashlib.sha256(Path(ant_noise.__file__).read_bytes()).hexdigest(),
    'source_sha256': hashlib.sha256((out/'heightmap.r16').read_bytes()).hexdigest(),
    'size': size, 'world_size': world, 'height_scale': height,
    'format': 'unsigned 16-bit little-endian, row-major; row increases in world +Z',
    'noise_properties': p, 'grid_cells': 32, 'max_depth': 5
}
(out/'source.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')

# A compact, editable Blender preview of the actual generated field.
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
step, n = 4, 257
vertices = [(world*(x*step/(size-1)-0.5), world*(z*step/(size-1)-0.5), float(h[z*step,x*step])*height)
            for z in range(n) for x in range(n)]
faces = [(z*n+x, z*n+x+1, (z+1)*n+x+1, (z+1)*n+x) for z in range(n-1) for x in range(n-1)]
mesh = bpy.data.meshes.new('ANT terrain preview (full heightmap exported separately)')
mesh.from_pydata(vertices, [], faces)
mesh.update()
obj = bpy.data.objects.new('A.N.T. Landscape seed 17', mesh)
bpy.context.collection.objects.link(obj)
obj['generator'] = manifest['generator']
obj['heightmap_resolution'] = size
obj['noise_seed'] = 17
bpy.context.view_layer.objects.active = obj
obj.select_set(True)
for polygon in mesh.polygons: polygon.use_smooth = True
bpy.ops.wm.save_as_mainfile(filepath=str(out/'terrain_source.blend'))
print('A.N.T. height field and Blender source saved', flush=True)
