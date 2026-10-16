#!/usr/bin/env python3
"""Render every preset through the original, packaged VST2, and packaged AU."""
from pathlib import Path
import subprocess, struct, math
root=Path(__file__).resolve().parent
build=root/'build'
vst=build/'Plugins/CamelCrusher.vst'
original=vst/'Contents/Resources/CamelCrusher.vst/Contents/MacOS/CamelCrusher'
bank=vst/'Contents/Resources/CamelCrusherPresetsA.fxb'
au=build/'Plugins/CamelCrusher.component'
log=[]
for preset in range(20):
    outputs=[]
    for tool,args in [('dump_orig',[str(original),str(build/'original.raw'),str(preset),'1',str(bank)]),
                      ('dump_brdg',[str(vst/'Contents/MacOS/CamelCrusher'),str(build/'vst.raw'),str(preset),'1']),
                      ('audump',[str(build/'au.raw'),str(preset),'1',str(au)])]:
        subprocess.run([str(build/tool)]+args,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        output=Path(args[0] if tool=='audump' else args[1]).read_bytes()
        assert len(output)==819200
        assert all(math.isfinite(v[0]) for v in struct.iter_unpack('f',output))
        outputs.append(output)
    assert outputs[0]==outputs[1]==outputs[2],f'Audio mismatch on preset {preset}'
    line=f'Preset {preset:02}: original = VST2 = AU, 204800 identical finite samples'
    print(line,flush=True);log.append(line)
(build/'audio-verification.txt').write_text('\n'.join(log)+'\n')
