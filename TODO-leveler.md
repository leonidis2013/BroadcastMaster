# TODO (not started, do not compile or push until told): auto-leveler misbehaves in real use

Reported Sun 2026-10-11 17:37, on the 0.4.1 build, YouTube (-14 LUFS) preset, music playing.
Screenshot (kept at /root/.claude/uploads/c3632c16-11cd-59bc-9aae-b8bbcb468a3a/4d76d325-image.png) shows:
- Leveler panel: Target -14.0 LUFS, Range 10.6 dB, Speed Medium, Gate -50 LUFS, Content Auto. "Gain applied: +6.4 dB", "Content: 0% speech-like".
- Output loudness: integrated -14.2, short-term -12.3, momentary -11.6, i.e. the programme sits about 2 dB ABOVE the -14 target.
- 60 s loudness history: level falls to about -30 LUFS for roughly 5 s, then jumps back to about -12 at once.
- Gain reduction strip: comp active, limiter line at the top.

Things to check when work resumes (hypotheses, none confirmed):
1. The leveler aims its OWN output at the target, but the stages after it (stereo utilities, soft clipper, limiter, exciter upstream) change loudness, so the measured output does not land on target. Consider measuring the target at the plug-in output, or compensating for the downstream stages.
2. +6.4 dB of lift with a -14 target means the leveler input is about -20 LUFS; confirm what the input level really is and that the K-weighted measure matches the output loudness meter (mono/stereo sum, channel weights).
3. The dip to -30 LUFS then instant recovery: check what the leveler did during and after the dip (hold, re-seed after quiet, boost phase), and whether the gain jumped or the source did.
4. Check the speech/music detector: 0% speech-like on content that includes a quiet passage.
5. Add a leveler debug readout (input level, activity state, held/active) to make this visible, and add tests that run the whole chain and check the OUTPUT loudness against the target.
