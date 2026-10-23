"""Render every recorded battle sound offline and compare with the PS1 recording (needs opencross exe; env OC_EXE overrides)."""
import sys, os, subprocess, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from x2_audio import load, load_wav
EXE = os.environ.get('OC_EXE', r'build\win-x64\bin\Debug\opencross.exe')
CASES = [  # scene, folder, tag, t_event, dur, vol, pan(0..127 as the tool takes it)
 ('x2b/serge_attack', 'player/selju2', '_s00', 3.511, 1.0, 107, 74),
 ('x2b/chain3', 'player/selju2', '_l01', 5.734, 1.2, 109, 74),
 ('x2b/kill_strong', 'player/selju2', '_l00', 3.511, 0.9, 107, 75),
 ('x2b/kill', 'player/selju2', '_s01', 11.117, 1.0, 106, 56),
 ('x2b/kill', 'battle/battle.prd', 'dead', 4.664, 2.0, 105, 78),
 ('x2b/kill', 'battle/battle.prd', 'dead', 12.321, 2.0, 102, 56),
 ('x2c/fireball', 'battle/battle.prd', 'sing', 10.749, 2.4, 127, 64),
 ('x2c/fireball', 'effects/red/9boll.prd', 'se60', 13.341, 3.5, 127, 64),
 ('x2c/ice_lance', 'effects/blue/4rance.prd', 'se60', 12.824, 3.5, 127, 64),
 ('x2c/heal', 'effects/green/101cure.prd', 'se60', 12.555, 3.5, 127, 64),
 ('x2c/turn_white', 'effects/white/88filw.prd', 'se60', 12.552, 3.5, 127, 64),
 ('x2c/earthquake', 'effects/yellow/27earth.prd', 'se61', 12.840, 4.0, 127, 64),
 ('x2c/earthquake', 'effects/yellow/27earth.prd', 'se60', 17.436, 5.0, 127, 64),
 ('x2c/salamander', 'effects/syoukan/saraman.prd', 'se70', 14.110, 7.0, 127, 64),
 ('x2c/salamander', 'effects/syoukan/saraman.prd', 'se71', 21.978, 5.0, 127, 64),
 ('x2c/puffy_needlework', 'effects/monster/hari_bon.prd', 'se87', 2.641, 3.0, 127, 64),
 ('x2c/puffy_needlework', 'battle/battle.prd', 'msin', 0.418, 2.0, 127, 64),
 ('x2c/bg21_cast', 'effects/red/11boru.prd', 'se61', 23.916, 4.0, 127, 64),
]
BANDS = [(0, 300), (300, 1000), (1000, 3000), (3000, 8000), (8000, 22050)]
def bands(x):
    n = len(x); S = np.abs(np.fft.rfft(x * np.hanning(n))) ** 2; f = np.fft.rfftfreq(n, 1 / 44100)
    return [10 * np.log10(S[(f >= a) & (f < b)].sum() + 1e-9) for a, b in BANDS]
def env(a, n=4410):
    k = len(a) // n; return np.sqrt((a[:k * n].reshape(k, n) ** 2).mean(1))
only = sys.argv[1] if len(sys.argv) > 1 else ''
print("case                                   rmsP1  rmsOurs  ratio  envcorr | band dB diff ours-ps1 (<300, -1k, -3k, -8k, >8k)")
for scene, folder, tag, t, dur, vol, pan in CASES:
    if only and only not in scene + tag: continue
    w = os.path.join('tmp', 'f', 'b_' + scene.split('/')[-1] + '_' + tag + '.wav')
    e = dict(os.environ, OC_SFX_WAV=w, OC_SFX_VOL=str(vol), OC_SFX_PAN=str(pan))
    subprocess.run([EXE, 'ocbattleseq', '-g', '..', 'sfx', folder, tag, os.environ.get('OC_WC', '0'), str(int(dur + 1))], env=e, capture_output=True)
    p = load(os.path.join('tmp', scene, 'audio.raw')); o, _ = load_wav(w)
    a = p[int(t * 44100):int((t + dur) * 44100)].mean(1); b = o[:int(dur * 44100)].mean(1)
    n = min(len(a), len(b)); a, b = a[:n], b[:n]
    ra, rb = np.sqrt((a ** 2).mean()), np.sqrt((b ** 2).mean())
    ea, eb = env(a), env(b); m = min(len(ea), len(eb)); c = np.corrcoef(ea[:m], eb[:m])[0, 1]
    bd = [y - x for x, y in zip(bands(a), bands(b))]
    print(f"{scene.split('/')[-1][:18]:18s} {tag:5s} {ra:8.4f} {rb:8.4f} {rb/ra:6.2f} {c:7.3f} | " + " ".join(f"{x:+5.1f}" for x in bd))
