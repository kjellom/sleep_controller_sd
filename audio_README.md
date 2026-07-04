# Audio files

Place WAV files in the SD card root directory:

- `audio_a.wav` — left channel → TDA2050 A → speaker A
- `audio_b.wav` — right channel → TDA2050 B → speaker B

## Required format

| Parameter | Value |
|---|---|
| Sample rate | 22050 Hz |
| Bit depth | 16-bit |
| Channels | Mono |
| Format | PCM WAV (uncompressed) |
| Encoding | Signed integer |

## Exporting from Audacity

1. Tracks → Stereo to Mono (if source is stereo)
2. Set sample rate to 22050 Hz (bottom left of Audacity window)
3. File → Export → Export as WAV
4. Format: WAV (Microsoft), Encoding: Signed 16-bit PCM
5. Rename to audio_a.wav or audio_b.wav
6. Copy to SD card root

## Loop quality

For seamless looping, match the amplitude and phase at the file boundaries:
- In Audacity, zoom to the end of the file and compare waveform to the beginning
- A click at the loop point means the waveform doesn't match — crossfade or trim
- Rain and white noise loop more cleanly than recordings with distinct events

## Sources

- freesound.org — CC-licensed nature recordings, filter by Loop tag
- Search: "rain loop", "rain ambience", "rainfall"
- Download as WAV, convert to 22050Hz 16-bit mono in Audacity

## File size reference

At 22050Hz 16-bit mono:
- 1 minute = ~2.6 MB
- 5 minutes = ~13 MB
- 30 minutes = ~75 MB

A 30-second to 2-minute loop is sufficient for ambient use.
