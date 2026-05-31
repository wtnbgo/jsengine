#!/usr/bin/env python3
# Demo 11 用の簡易 BGM/SE WAV を合成して data/bgm/, data/se/ に書き出す。
# 標準ライブラリのみ (wave, math, struct, random)。
# 実行: python tools/gen_audio_assets.py

import math, struct, wave, random, os

SR = 44100
OUTDIR_BGM = os.path.join(os.path.dirname(__file__), "..", "data", "bgm")
OUTDIR_SE  = os.path.join(os.path.dirname(__file__), "..", "data", "se")
os.makedirs(OUTDIR_BGM, exist_ok=True)
os.makedirs(OUTDIR_SE,  exist_ok=True)


def write_wav(path, samples, sr=SR):
    # samples: iterable of float in [-1, 1]
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        frames = bytearray()
        for s in samples:
            v = max(-1.0, min(1.0, s))
            frames += struct.pack("<h", int(v * 32760))
        w.writeframes(bytes(frames))
    print(f"wrote {path}  ({len(samples)} samples = {len(samples)/sr:.2f}s)")


def sine(freq, t):
    return math.sin(2 * math.pi * freq * t)


def square(freq, t, duty=0.5):
    p = (freq * t) % 1.0
    return 1.0 if p < duty else -1.0


def triangle(freq, t):
    p = (freq * t) % 1.0
    return 4.0 * abs(p - 0.5) - 1.0


def env_adsr(t, dur, a=0.01, d=0.05, s=0.7, r=0.1):
    if t < a: return t / a
    if t < a + d: return 1.0 - (1.0 - s) * ((t - a) / d)
    if t < dur - r: return s
    if t < dur: return s * (1.0 - (t - (dur - r)) / r)
    return 0.0


# ============================================================
# BGM: title — calm pad chord progression (loopable)
# ============================================================
# 8 小節 / BPM 90 / Cmaj7 → Am7 → Fmaj7 → G7  (2 小節ずつ)
def make_title_bgm():
    bpm = 90
    beat = 60.0 / bpm
    bar = 4 * beat
    total_bars = 8
    dur = bar * total_bars
    n = int(SR * dur)
    samples = [0.0] * n

    # コード進行 (周波数 Hz)
    # C4=261.63, E4=329.63, G4=392.00, B4=493.88
    # A3=220.00, C4=261.63, E4=329.63, G4=392.00
    # F3=174.61, A3=220.00, C4=261.63, E4=329.63
    # G3=196.00, B3=246.94, D4=293.66, F4=349.23
    chords = [
        [261.63, 329.63, 392.00, 493.88],
        [220.00, 261.63, 329.63, 392.00],
        [174.61, 220.00, 261.63, 329.63],
        [196.00, 246.94, 293.66, 349.23],
    ]
    chord_dur = bar * 2  # 2 小節ずつ

    for i in range(n):
        t = i / SR
        ci = int((t // chord_dur) % 4)
        chord = chords[ci]
        # 各音をサイン + 三角でうっすら鳴らす
        v = 0.0
        for f in chord:
            v += 0.10 * sine(f, t)
            v += 0.03 * triangle(f * 2.0, t)  # 1 オクターブ上のキラキラ
        # 全体に LFO で揺らぎ
        lfo = 0.95 + 0.05 * sine(0.25, t)
        v *= lfo
        # フェードイン (最初の 0.5s) と ループ用フェード (最後 0.3s)
        if t < 0.5:
            v *= t / 0.5
        if t > dur - 0.3:
            v *= (dur - t) / 0.3
        samples[i] = v * 0.55

    write_wav(os.path.join(OUTDIR_BGM, "title.wav"), samples)


# ============================================================
# BGM: game — chiptune bass + arpeggio (loopable)
# ============================================================
def make_game_bgm():
    bpm = 130
    beat = 60.0 / bpm
    sixteenth = beat / 4
    bar = 4 * beat
    total_bars = 4
    dur = bar * total_bars
    n = int(SR * dur)
    samples = [0.0] * n

    # ベース: 各小節の頭 4 拍 = C2, A1, F2, G2
    bass_notes = [65.41, 55.00, 87.31, 98.00]
    # アルペジオ: 16th で 4 音をぐるぐる (chord に合わせ)
    chord_arps = [
        [261.63, 329.63, 392.00, 523.25],  # C E G C
        [220.00, 261.63, 329.63, 440.00],  # A C E A
        [174.61, 220.00, 349.23, 440.00],  # F A F A (3rd skipped for variety)
        [196.00, 246.94, 293.66, 392.00],  # G B D G
    ]

    for i in range(n):
        t = i / SR
        bar_idx = int((t // bar) % 4)
        # ベース矩形波
        bass_freq = bass_notes[bar_idx]
        beat_pos = (t % beat) / beat  # 0..1
        bass_env = max(0.0, 1.0 - beat_pos * 1.2)
        bass = 0.18 * square(bass_freq, t, 0.5) * bass_env

        # アルペジオ (16th 切り)
        sixteenth_idx = int(t / sixteenth) % 4
        arp_freq = chord_arps[bar_idx][sixteenth_idx]
        sub_t = (t % sixteenth)
        arp_env = max(0.0, 1.0 - sub_t / sixteenth)
        arp_env = arp_env * arp_env  # 早く減衰
        arp = 0.10 * triangle(arp_freq, t) * arp_env

        # ハイハット風ノイズ (8th オフビート)
        eighth = beat / 2
        eighth_pos = (t % eighth) / eighth
        hat_env = max(0.0, 1.0 - eighth_pos * 6.0) if ((int(t / eighth) % 2) == 1) else 0.0
        hat = 0.04 * (random.random() * 2 - 1) * hat_env

        v = bass + arp + hat
        # フェードイン/アウトでループ繋ぎ
        if t < 0.2: v *= t / 0.2
        if t > dur - 0.2: v *= (dur - t) / 0.2
        samples[i] = v * 0.8

    write_wav(os.path.join(OUTDIR_BGM, "game.wav"), samples)


# ============================================================
# SE: select — short high beep (menu navigation)
# ============================================================
def make_se_select():
    dur = 0.06
    n = int(SR * dur)
    samples = []
    for i in range(n):
        t = i / SR
        e = env_adsr(t, dur, 0.002, 0.01, 0.6, 0.04)
        samples.append(0.35 * square(1200, t) * e)
    write_wav(os.path.join(OUTDIR_SE, "select.wav"), samples)


# ============================================================
# SE: confirm — rising blip
# ============================================================
def make_se_confirm():
    dur = 0.18
    n = int(SR * dur)
    samples = []
    for i in range(n):
        t = i / SR
        # 周波数を 600 → 1200 にスライド
        freq = 600 + (1200 - 600) * (t / dur)
        e = env_adsr(t, dur, 0.005, 0.03, 0.7, 0.08)
        v = 0.20 * square(freq, t) + 0.20 * sine(freq * 2, t)
        samples.append(v * e)
    write_wav(os.path.join(OUTDIR_SE, "confirm.wav"), samples)


# ============================================================
# SE: cancel — falling blip
# ============================================================
def make_se_cancel():
    dur = 0.22
    n = int(SR * dur)
    samples = []
    for i in range(n):
        t = i / SR
        freq = 900 - (900 - 350) * (t / dur)
        e = env_adsr(t, dur, 0.005, 0.04, 0.6, 0.1)
        v = 0.25 * square(freq, t) + 0.15 * sine(freq * 0.5, t)
        samples.append(v * e)
    write_wav(os.path.join(OUTDIR_SE, "cancel.wav"), samples)


# ============================================================
# SE: fire — short percussive blip (score +1)
# ============================================================
def make_se_fire():
    dur = 0.12
    n = int(SR * dur)
    samples = []
    for i in range(n):
        t = i / SR
        # 1500 → 400 に急減速
        freq = 1500 * math.exp(-t * 10)
        # noise mix で重み付け
        e = env_adsr(t, dur, 0.001, 0.02, 0.5, 0.08)
        v = 0.30 * square(freq, t) + 0.10 * (random.random() * 2 - 1) * max(0.0, 1.0 - t * 30)
        samples.append(v * e)
    write_wav(os.path.join(OUTDIR_SE, "fire.wav"), samples)


# ============================================================
# SE: pause — short low chord
# ============================================================
def make_se_pause():
    dur = 0.18
    n = int(SR * dur)
    samples = []
    for i in range(n):
        t = i / SR
        e = env_adsr(t, dur, 0.005, 0.04, 0.6, 0.1)
        v = 0.15 * (sine(261.63, t) + sine(329.63, t) + sine(392.00, t)) / 3.0
        samples.append(v * e)
    write_wav(os.path.join(OUTDIR_SE, "pause.wav"), samples)


if __name__ == "__main__":
    random.seed(42)
    make_title_bgm()
    make_game_bgm()
    make_se_select()
    make_se_confirm()
    make_se_cancel()
    make_se_fire()
    make_se_pause()
    print("done")
