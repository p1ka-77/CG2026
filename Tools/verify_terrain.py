"""Offline checks for generated source tiles and the runtime terrain container."""
from pathlib import Path
import hashlib
import json
import struct
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[1]
folder = root / 'Assets' / 'Terrain'
meta = json.loads((folder / 'source.json').read_text())
raw_bytes = (folder / 'heightmap.r16').read_bytes()
assert hashlib.sha256(raw_bytes).hexdigest() == meta['source_sha256']
assert hashlib.sha256((root/'Tools/vendor/ant_noise.py').read_bytes()).hexdigest() == meta['generator_sha256']
n = meta['size']
raw = np.frombuffer(raw_bytes, dtype='<u2').reshape(n, n)
assert np.array_equal(np.asarray(Image.open(folder/'heightmap.png')), raw)
source_tiles = {}
for z in range(4):
    for x in range(4):
        tile = np.asarray(Image.open(folder/'HeightTiles'/f'tile_{x}_{z}.png'))
        assert tile.shape == (257, 257)
        assert np.array_equal(tile, raw[z*256:z*256+257, x*256:x*256+257])
        source_tiles[x, z] = tile
for z in range(4):
    for x in range(4):
        if x < 3: assert np.array_equal(source_tiles[x,z][:,-1], source_tiles[x+1,z][:,0])
        if z < 3: assert np.array_equal(source_tiles[x,z][-1,:], source_tiles[x,z+1][0,:])
print('Source SHA256, 16 PNG16 tiles, exact shared edges: PASS')

data = (folder/'terrain.bin').read_bytes()
magic, version, count, grid, depth, side, size, world, height = struct.unpack_from('<4s6I2f', data)
assert (magic, version, grid, depth, side, size) == (b'TRN1', 1, 32, 5, 35, n)
assert count == 1365
assert len(data) == 36 + count*48 + count*side*side*2
tiles = np.frombuffer(data, dtype='<u2', offset=36+count*48).reshape(count, side, side)
for i in range(count):
    node = struct.unpack_from('<6f4iIf', data, 36+i*48)
    ox, oz, span, low, high, error = node[:6]
    children, level, skirt = node[6:10], node[10], node[11]
    x = round((ox/world + 0.5)*(n-1))
    z = round((oz/world + 0.5)*(n-1))
    step = 1 << (depth-level)
    sx = np.clip(x + np.arange(-1, grid+2)*step, 0, n-1)
    sz = np.clip(z + np.arange(-1, grid+2)*step, 0, n-1)
    assert np.array_equal(tiles[i], raw[np.ix_(sz,sx)])
    assert high-skirt < 0 and error >= 0
    if level == depth:
        assert children == (-1,-1,-1,-1) and error < 0.001
    else:
        assert all(i < c < count for c in children)
print('1365 runtime tiles, aprons, leaf errors, skirts and container size: PASS')

# Optional actual-GPU frames from Crate.exe --terrain-capture=0..5.
for ppm in root.glob('terrain_capture_*.ppm'):
    with Image.open(ppm) as frame:
        frame.save(ppm.with_suffix('.png'))
        assert np.asarray(frame).std() > 2, f'Unexpected uniform frame: {ppm.name}'
        print(f'GPU frame converted and non-uniform: {ppm.with_suffix(".png").name}')
