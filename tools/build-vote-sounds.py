"""Create the mod's short original vote notification sounds (no external assets)."""
import math
import struct
import wave
from pathlib import Path

root = Path(__file__).resolve().parents[1]/'game/mod_hl2mp/sound/hl2mp'
root.mkdir(parents=True, exist_ok=True)
for name, notes in {
    'vote_start': [(660, .10), (880, .14)],
    'vote_cast': [(740, .07)],
    'vote_yes': [(740, .055), (930, .075)],
    'vote_no': [(490, .055), (370, .075)],
    'vote_pass': [(660, .10), (880, .10), (1100, .20)],
    'vote_fail': [(440, .14), (330, .22)],
}.items():
    samples = []
    for frequency, duration in notes:
        count = int(44100 * duration)
        for i in range(count):
            t = i / 44100
            envelope = min(1, i / 220) * min(1, (count - i) / 1200)
            samples.append(int(7000 * envelope * (math.sin(2*math.pi*frequency*t) + .15*math.sin(4*math.pi*frequency*t))))
        samples += [0] * 660
    with wave.open(str(root/(name+'.wav')), 'wb') as out:
        out.setparams((1, 2, 44100, 0, 'NONE', 'not compressed'))
        out.writeframes(struct.pack('<' + 'h'*len(samples), *samples))
    print(name)
