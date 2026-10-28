"""Independent check of the model renderer's PVR stream over a recorded script.

Builds the host submission simulator (build_sim.py) and compares every frame,
triangle by triangle, with the reference renderer tests/reference_stream.h,
at scenery stages 3 and 5 (and 6 for a world pack with world objects). Needs prepared assets (car.dnm/.dnt, world.dnm/.dnt)
and a frame table; both are generated files outside the repository.

usage: check_stream.py [--fence-sweep] ASSETS_DIR FRAMES.frm [more FRAMES.frm ...]
           [--source UNCUT_ASSETS_DIR UNCUT_ORDER.json ORDER.json [SPEC.json]]

--fence-sweep builds the simulator with -DNM_FENCE_SWEEP=1 (make NATIVE_FENCE_SWEEP=1; the default is 0):
the catch-fence sweep, which acts only on a world pack with a fence rule's stipple groups (assets-ssfar4cf3 ..
assets-ssfar4eu; the recommended assets-ssfar4f has none).

The reference renders the pack it is given, so by itself it says nothing
about a world pack whose punch-through faces were cut offline into opaque
pieces (tools/opaque_cores.py): their triangles are legitimately not the
arcade's. --source adds the missing half, check_cutout_sources.py: every
changed face of ASSETS_DIR/world.dnm against its own source face in the
uncut pack (texels covered, colours, plane, layer), from the shipped files.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

if len(sys.argv) < 3:
    raise SystemExit(__doc__)
here = Path(__file__).resolve().parent
arguments, source = sys.argv[1:], None
defines = []
if '--fence-sweep' in arguments:
    arguments = [a for a in arguments if a != '--fence-sweep']
    defines.append('-DNM_FENCE_SWEEP=1')
if '--source' in arguments:
    at = arguments.index('--source')
    arguments, source = arguments[:at], arguments[at+1:]
    if len(source) not in (3, 4) or len(arguments) < 2:
        raise SystemExit(__doc__)
assets, frames = arguments[0], arguments[1:]
if source:
    run = subprocess.run([sys.executable, str(here/'check_cutout_sources.py'), source[0], assets, *source[1:]],
                         capture_output=True, text=True)
    last = (run.stdout.strip().splitlines() or [run.stderr[-2000:]])[-1]
    if run.returncode or not last.startswith('PASS'):
        print(run.stdout[-3000:] or run.stderr[-2000:])
        raise SystemExit('FAIL: a cut face does not draw what its source face drew')
    print('PASS: cut faces against their sources: ' + last[6:])
# A world pack with world objects (meshpack.h, NM_PACK_WORLD_OBJECTS) is also checked at stage 6;
# its frame tables must be FRM2 (cell bitmaps and race state for the object selection).
import struct
header = struct.unpack_from('<16I', (Path(assets)/'world.dnm').read_bytes())
stages = ('3', '5', '6') if (header[0] & 0x7fffffff) == 0x364d4e44 and header[15] & 8 else ('3', '5')
with tempfile.TemporaryDirectory(prefix='daytona-stream-') as tmp:
    subprocess.run([sys.executable, str(here/'build_sim.py'), tmp, 'sim', *defines], check=True, stdout=subprocess.DEVNULL)
    for table in frames:
        for stage in stages:
            run = subprocess.run([str(Path(tmp)/'sim'), assets, table, os.devnull],
                                 env=dict(os.environ, SIM_ORACLE='1', SIM_STAGE=stage), capture_output=True, text=True)
            lines = [line for line in run.stdout.splitlines() if 'reference' in line]
            if run.returncode or not any('independent reference' in line and ' 0 mismatches' in line for line in lines):
                print('\n'.join(lines[:12]) or run.stderr[-2000:])
                raise SystemExit(f'FAIL: stage {stage}, {table}: the submitted stream differs from the reference renderer')
            print(f'PASS: stage {stage}, {Path(table).name}: ' + lines[-1])
            translucent = [line for line in run.stdout.splitlines() if line.startswith('translucent list')]
            if translucent:
                print(f'PASS: stage {stage}, {Path(table).name}: ' + translucent[0])
    # The player's damaged bodies (car-damage.dnb): each written into the clean body's mesh and its tables refreshed must
    # give the tables a rebuild gives (SIM_DAMAGE_CHECK), and the wrecked body (variant 0) drawn all along the first
    # table must still be the reference renderer's stream.
    if (Path(assets)/'car-damage.dnb').is_file():
        run = subprocess.run([str(Path(tmp)/'sim'), assets, frames[0], os.devnull], capture_output=True, text=True,
                             env=dict(os.environ, SIM_ORACLE='1', SIM_STAGE=stages[-1], SIM_DAMAGE_CHECK='1', SIM_DAMAGE_VARIANT='0'))
        checks = [line for line in run.stdout.splitlines() if line.startswith('damage check:')]
        lines = [line for line in run.stdout.splitlines() if 'reference' in line]
        if (run.returncode or len(checks) != 17 or any('equal the rebuilt' not in line for line in checks) or
                not any('independent reference' in line and ' 0 mismatches' in line for line in lines)):
            print('\n'.join(checks+lines[:12]) or run.stderr[-2000:])
            raise SystemExit(f'FAIL: damaged bodies, {frames[0]}')
        print(f'PASS: damaged bodies: 17 variants refreshed as rebuilt; wrecked body, stage {stages[-1]}, '
              f'{Path(frames[0]).name}: ' + lines[-1])
