# Experimental full audio-timecode samples

Two **offline, synthetic-reference** signals from the native experiment:

- `fsk-1200-2hz-8k-mulaw.wav`: preferred 1200-baud candidate, eight packets,
  two events/s, approximately 4.4 seconds.
- `fsk-600-1hz-8k-mulaw.wav`: 600-baud comparison, eight packets,
  one event/s, approximately 8.4 seconds.

Both went through 300–3400-Hz filtering and actual FFmpeg G.711 µ-law encoding
at 8 kHz, then were decoded to PCM16 mono WAV for ordinary players. Each final
WAV was independently analyzed: 8/8 complete identities, exact 64-bit send times
and signed source AV offsets matched its manifest. The adjacent decoded JSON
contains the actual receiver output.

The source epoch is fixed for reproducibility. **Playing these recordings later
is not a wall-clock latency test.** There is no image track. The currently
released generator/iPhone analyzer do not yet decode this experimental payload.
Use the native probe built by `tests/audio-timecode/run.py` to inspect it:

```sh
/tmp/avsync-timecode-results/audio-timecode-probe analyze --input testfiles/audio-timecode/fsk-1200-2hz-8k-mulaw.wav
```

See `tests/audio-timecode/README.md` and `RESULTS-2026-10-26.md` for framing,
reproduction and limits. No real call/acoustic or mobile integration result is
implied by these sample files.
