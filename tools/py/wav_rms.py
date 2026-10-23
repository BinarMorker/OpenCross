"""RMS level of a 16-bit WAV in half-second windows, printed on one line as `seconds:rms`.

    python tools/py/wav_rms.py FILE.wav
"""
import wave,array,math,sys
w=wave.open(sys.argv[1]);n=w.getnframes();r=w.getframerate();ch=w.getnchannels()
a=array.array('h',w.readframes(n))
step=r//2
print(' '.join('%.1f:%d'%(i/r,math.sqrt(sum(x*x for x in a[i*ch:(i+step)*ch])/max(1,step*ch))) for i in range(0,n,step)))
