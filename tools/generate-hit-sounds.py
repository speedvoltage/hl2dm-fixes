"""Generate the mod's original, short hit-confirmation sounds (no sampled audio)."""
from pathlib import Path
import math
import random
import struct
import wave

ROOT = Path(__file__).resolve().parents[1]
RATE = 44100
# Match average levels across the choices, leaving at least 1 dB of peak headroom.
TARGET_RMS = 32767 * 10 ** (-11 / 20)
PEAK_LIMIT = 32767 * 10 ** (-1 / 20)
# Bell, click, pop, chime, glass, wood, metal, digital, pulse, pluck; then variants.
PRESETS = [
    (1150,.14,0),(2200,.055,1),(580,.08,2),(1320,.18,3),
    (1900,.12,4),(780,.075,5),(1550,.11,6),(1000,.09,7),
    (850,.10,8),(940,.13,9),(780,.16,0),(1550,.045,1),
    (900,.065,2),(1760,.15,3),(2400,.10,4),(1450,.075,7),
]

def samples(frequency, duration, timbre):
    rng=random.Random(813+timbre)
    output=[]
    for i in range(int(RATE*duration)):
        t=i/RATE;p=2*math.pi*frequency*t
        # A brief sustain and gentler decay keep confirmation audible under gunfire.
        env=min(1,t/.002)*math.exp(-max(0,t-.012)/(duration/2))
        env*=min(1,(duration-t)/.006)
        if timbre==0: value=math.sin(p)+.25*math.sin(2.76*p)*math.exp(-t*25)
        elif timbre==1: value=.65*math.sin(p)+.35*rng.uniform(-1,1)*math.exp(-t*100)
        elif timbre==2: value=math.sin(p*math.exp(-t*9))
        elif timbre==3: value=.65*math.sin(p)+.35*math.sin(1.5*p)
        elif timbre==4: value=math.sin(p)+.3*math.sin(3.13*p)*math.exp(-t*30)
        elif timbre==5: value=math.sin(p)+.4*math.sin(1.47*p)+.1*rng.uniform(-1,1)
        elif timbre==6: value=.6*math.sin(p)+.25*math.sin(2.4*p)+.15*math.sin(4.1*p)
        elif timbre==7: value=math.sin(p)+.2*math.sin(3*p)+.1*math.sin(5*p)
        elif timbre==8: value=math.sin(p)*(0.7+0.3*math.cos(2*math.pi*65*t))
        else: value=math.sin(p)+.35*math.sin(2*p)*math.exp(-t*35)+.12*math.sin(3*p)
        output.append(value*env)
    peak=max(abs(v) for v in output)
    rms=math.sqrt(sum(v*v for v in output)/len(output))
    gain=min(PEAK_LIMIT/peak,TARGET_RMS/rms)
    return [round(v*gain) for v in output]

def main():
    target=ROOT/'game/mod_hl2mp/sound/ui/hitsounds'
    target.mkdir(parents=True,exist_ok=True)
    for i,preset in enumerate(PRESETS):
        values=samples(*preset)
        with wave.open(str(target/f'hit_{i:02}.wav'),'wb') as output:
            output.setparams((1,2,RATE,len(values),'NONE','not compressed'))
            output.writeframes(struct.pack('<'+'h'*len(values),*values))
    print('Generated 16 original mono PCM hit sounds')

if __name__=='__main__': main()
