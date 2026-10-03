"""Pack the generator's shared height field into quadtree tiles with 1-texel aprons.
Requires numpy and Pillow. Also exports sixteen overlapping PNG16 source tiles.
"""
from pathlib import Path
import json, struct
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[1]
out = root/'Assets'/'Terrain'
manifest = json.loads((out/'source.json').read_text())
n, world, height = manifest['size'], manifest['world_size'], manifest['height_scale']
grid, depth = manifest['grid_cells'], manifest['max_depth']
assert n-1 == grid * (1 << depth)
raw = np.fromfile(out/'heightmap.r16', dtype='<u2').reshape(n,n)
heights = raw.astype(np.float64)/65535*height
tile_size = grid+3
nodes, tiles = [], []

def build(x,z,span,level):
    index = len(nodes)
    nodes.append(None)
    tiles.append(None)
    step = span//grid
    patch = heights[z:z+span+1,x:x+span+1]
    coords = np.arange(span+1)/step
    cell = np.minimum(coords.astype(np.int32),grid-1)
    frac = coords-cell
    coarse = patch[::step,::step]
    a = coarse[cell[:,None],cell[None,:]]
    b = coarse[cell[:,None],cell[None,:]+1]
    c = coarse[cell[:,None]+1,cell[None,:]]
    d = coarse[cell[:,None]+1,cell[None,:]+1]
    u,v = frac[None,:],frac[:,None]
    # Match the GPU's diagonal from (0,0) to (1,1), not bilinear interpolation.
    approximate = np.where(u>=v,a+(b-a)*u+(d-b)*v,a+(d-c)*u+(c-a)*v)
    error = float(np.abs(patch-approximate).max())
    # No 2:1 neighbour constraint: curtains must also cover larger LOD jumps.
    # Put every curtain bottom below the global minimum, including coarse edges.
    skirt = float(patch.max()-heights.min())+3.0
    children = [-1]*4
    if level<depth:
        half = span//2
        for k,(dx,dz) in enumerate(((0,0),(half,0),(0,half),(half,half))):
            children[k] = build(x+dx,z+dz,half,level+1)
    nodes[index] = (world*(x/(n-1)-0.5),world*(z/(n-1)-0.5),world*span/(n-1),
                    float(patch.min()),float(patch.max()),error,*children,level,skirt)
    sample_x = np.clip(x + np.arange(-1,grid+2)*step,0,n-1)
    sample_z = np.clip(z + np.arange(-1,grid+2)*step,0,n-1)
    tiles[index] = raw[np.ix_(sample_z,sample_x)].astype('<u2').tobytes()
    return index

build(0,0,n-1,0)
with (out/'terrain.bin').open('wb') as f:
    f.write(struct.pack('<4s6I2f',b'TRN1',1,len(nodes),grid,depth,tile_size,n,world,height))
    for node in nodes: f.write(struct.pack('<6f4iIf',*node))
    for tile in tiles: f.write(tile)
png_dir=out/'HeightTiles'
png_dir.mkdir(exist_ok=True)
tile_cells=(n-1)//4
for z in range(4):
    for x in range(4):
        Image.fromarray(raw[z*tile_cells:(z+1)*tile_cells+1,x*tile_cells:(x+1)*tile_cells+1]).save(png_dir/f'tile_{x}_{z}.png')
Image.fromarray(raw).save(out/'heightmap.png')
print(f'Packed {len(nodes)} nodes, {tile_size}x{tile_size} R16 height tiles; world={world}m, height={height}m')
