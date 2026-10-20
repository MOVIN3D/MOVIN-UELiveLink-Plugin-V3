# Changelog

## 3.3.1 — 2026-10-20

Recommended for MOVIN Studio v3.3.0 or later.

- Add per-source Global Rotation Offset settings with explicit X/Roll, Y/Pitch and Z/Yaw inputs in Unreal degrees.
- Rotate facing and the complete travel path about the stream origin, before the Skeletal Mesh component transform.
- Apply the offset once at the top-level streamed bone, preserving the existing root basis and descendant local poses.
- Support editing offsets while connected and saving/restoring them through Live Link Presets; older presets default to zero.
- Add UDP/LiveLink regression coverage for each axis, rotated root hierarchies, preset restoration and non-cumulative frame updates.

## 3.3.0 — 2026-09-30

Recommended for MOVIN Studio v3.3.0 or later. This release was prepared internally as v1.2.0;
the published version follows the Studio release family.

- Add Studio status replies with endpoint matching, skeleton identity and receive/LiveLink FPS.
- Validate complete binary datagrams, UTF-8 names, unique Unreal bone names and finite transforms.
- Ignore duplicate/out-of-order frames; recover a restarted sender after one second of inactivity.
- Fix receiver thread shutdown, port collision handling and stale subjects after character changes.
- Keep Actor calibration tied to the enabled LiveLink source; discard stopped-source calibration.
- Preserve world-motion classification while idle so pelvis movement is excluded from bone-length comparisons.
- Remove Actor bone-length offset popups; retain automatic fitting and Output Log diagnostics.
- Disable internal stream validation by default and exclude it and test sources from user packages.
- Add versioned packaging, source fingerprints, checksums and UDP/LiveLink regression tests.

Studio's companion update writes proper UTF-8 and 7-bit string lengths. The motion format is
unchanged for existing ASCII names below 128 bytes. New telemetry requires both updated sides.

## 3.0.0 — 2026-09-30

- Rename the v1.1.0 release to match the Studio v3.0.0-v3.2.0 family.
- Update the plugin descriptor, package names and documentation; runtime binaries are unchanged.
- Retain the original v1.1.0 tag and release downloads.
