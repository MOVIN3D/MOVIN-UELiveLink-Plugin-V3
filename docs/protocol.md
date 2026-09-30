# UDP Packet Format

Reference for the wire format the plugin expects from MOVIN Studio. You do not need any of this to
use the plugin - it is here for anyone writing a sender or debugging a stream at the packet level.

All values are **little-endian**. Strings use the **C# `BinaryWriter` 7-bit encoded length prefix**.

| Field | Size | Description |
| --- | --- | --- |
| `packetSize` | 4 bytes (int32) | Total size of the datagram body |
| `subjectName` | variable | 7-bit length prefix + UTF-8 string |
| `frameIdx` | 4 bytes (int32) | Nonnegative frame index |
| `boneCount` | 4 bytes (int32) | Number of bones |
| **Per bone** (repeated `boneCount` times): | | |
| `boneName` | variable | 7-bit length prefix + UTF-8 string |
| `localPosition` | 12 bytes (3 x float) | X, Y, Z position |
| `localRotation` | 16 bytes (4 x float) | X, Y, Z, W quaternion |
| `localScale` | 12 bytes (3 x float) | X, Y, Z scale |

> **Coordinate system:** the plugin converts from Unity's Y-up left-hand coordinate system (X, Y, Z)
> to Unreal's Z-up left-hand system (Z, X, Y) automatically.

## Validation and frame ordering

`packetSize` must equal datagram length minus 4. The maximum datagram is 65,507 bytes.
There must be 1-300 bones, exactly the declared payload, and no trailing bytes. Subject and bone
names must be nonempty valid UTF-8, at most 256 UTF-16 code units, without NUL. `None` is reserved.
Bone names must be unique under Unreal's case-insensitive `FName` comparison. Position, rotation
and scale must be finite. Zero-length quaternions are rejected; other rotations are normalized.

Studio sends positions in centimetres after applying its export settings. The receiver permutes
position, quaternion XYZ and scale from `(X,Y,Z)` to `(Z,X,Y)` without another unit conversion.
The format does not carry parent indices. LiveLink static data is a flattened hierarchy; the
normal animation path uses the target Skeletal Mesh's hierarchy and matches bones by name.

Only increasing indices from the active UDP endpoint are accepted. After 1 second without an
accepted frame, any nonnegative index can start a new sequence. Invalid packets and status probes
do not extend that timeout. This bounds restart delay but does not identify sessions: an extremely
late datagram arriving after the idle timeout can still become the new first frame.

All bones in a motion frame occupy one UDP datagram. A lost/truncated datagram therefore loses the
whole frame; there is no partial-frame reassembly. Skeleton changes publish static data first and
skip that packet's animation data; the following accepted frame uses the new layout.

## Studio status (plugin 3.3.0+, Studio 3.3.0+)

Status uses standard OSC (big-endian numeric fields, UTF-8 NUL-terminated strings padded to 4 bytes)
on the **same listening port** as binary motion. It does not change the motion format.

Request: `/MOVIN/Unreal/Status/Request`, type tags `,sii`:

| Argument | Type | Meaning |
| --- | --- | --- |
| token | string | 32 hex characters identifying this probe |
| replyPort | int | Studio's listening port, normally 39581; 1-65535 |
| motionPort | int | Source port of Studio's motion socket; 0 if it has not sent yet |

The plugin replies to the request sender's IP at `replyPort`, at most four times per second.
Only the latest pending probe is retained. Studio probes once per second and accepts only a reply
matching the outstanding token. This is local-network telemetry, not authentication.

Reply: `/MOVIN/Unreal/Status`, type tags `,sisiifffisi`:

| Index | Argument | Type | Meaning |
| --- | --- | --- | --- |
| 0 | token | string | Echoed probe token |
| 1 | version | int | 1 |
| 2 | subject | string | Last accepted subject; empty before first motion |
| 3 | frame | int | Last accepted frame; -1 when unknown |
| 4 | boneCount | int | Last accepted bone count; 0 when unknown |
| 5 | receivedFps | float | Accepted motion frames per second |
| 6 | publishedFps | float | Frames submitted to LiveLink per second |
| 7 | motionAge | float | Seconds since last accepted frame; -1 when unknown |
| 8 | errors | int | Invalid datagrams rejected since source creation |
| 9 | boneSignature | string | Canonical SHA-256 below; empty when unknown |
| 10 | sameSource | int | 1 if request IP + motionPort matches the accepted motion endpoint, else 0 |

Rates are sampled over at least 0.25 seconds and become zero after 1 second without motion.
Published FPS counts submission to LiveLink, not final mesh application or rendering.

For `boneSignature`, sort the original bone names using ordinal case-sensitive UTF-16 order,
write each as a C# BinaryWriter string (7-bit UTF-8 byte length + UTF-8 bytes), concatenate and
SHA-256 hash. Send lowercase hexadecimal. `Hips`, `Root` gives
`9586db745adfcdb553e8c9c30b1a17b8c6280e4500ba372feac94418784b55a1`.
