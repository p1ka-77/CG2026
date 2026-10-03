# A.N.T. Landscape source used by the terrain generator

`ant_noise.py` is an **unmodified** source file from Blender's official add-ons repository:

https://github.com/blender/blender-addons/blob/main/ant_landscape/ant_noise.py

Source file retrieved on 2026-09-30. SHA-256:
`b4dab3820a429df0f85c163a9ab7a7c5755de3aebdad6728c025a4eb96fc9bf2`

It retains its original copyright and GPL-2.0-or-later notice. See `COPYING` for the GPL version 2 text. Preserve this source, its notices and the license when redistributing these generator tools. This Python module is invoked by Blender during offline asset generation; it is not compiled or linked into the DirectX application.

The official current extension page is https://extensions.blender.org/add-ons/antlandscape/ . The lab uses the vendored source identified above, not an automatically updated extension installation.

`../generate_ant_terrain.py` calls `ant_noise.noise_gen` with documented properties. `Assets/Terrain/source.json` records all properties and hashes. The generated `.blend` is a reduced-resolution editable preview of the same height field; the full-resolution source is `heightmap.r16`.
