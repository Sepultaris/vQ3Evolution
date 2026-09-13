"""Compile production NV submission functions; no game/window/settings changes."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / 'code/renderer_vulkan'

def function(source, name):
    start = source.index(name + '(') if name + '(' in source else source.index(name + ' (')
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

def run(cc, sdk):
    commands = (RENDERER/'tr_cmds.c').read_text()
    main = (RENDERER/'tr_main.c').read_text()
    bodies = ''.join(function(commands,n) for n in ('R_NvOverlaySet','R_NvOverlayActive',
        'R_NvOverlayClear','R_NvReplaceOverlay','RE_StretchPic','RE_BeginFrame'))
    bodies += function(main,'R_AddDrawSurf')
    # Check suppression is conditional, rather than hiding the mod's old NV
    # when the replacement is disabled/unavailable.
    assert 'shader->nvOverlay && R_NvReplaceOverlay()' in commands
    assert 'cmd->shader->nvOverlay && R_NvReplaceOverlay()' in commands
    assert 'discard_commands:\n\tR_NvOverlayClear();' in commands
    with tempfile.TemporaryDirectory(prefix='nv-activation-') as temp:
        folder=Path(temp)
        (folder/'nv_submission.inc').write_text(bodies)
        exe=folder/'check.exe'
        subprocess.run([cc,'-std=c99','-O1',str(ROOT/'tests/nv_activation_fixture.c'),
            '-I',str(RENDERER),'-I',str(sdk/'Include'),'-I',str(folder),'-o',str(exe)],check=True,timeout=30)
        subprocess.run([str(exe)],check=True,timeout=10)

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',required=True)
    p.add_argument('--sdk',required=True,type=Path)
    a=p.parse_args(); run(a.cc,a.sdk)
