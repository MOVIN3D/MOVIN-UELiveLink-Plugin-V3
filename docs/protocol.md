# UDP Packet Format

Reference for the wire format the plugin expects from MOVIN Studio. You do not need any of this to
use the plugin - it is here for anyone writing a sender or debugging a stream at the packet level.

All values are **little-endian**. Strings use the **C# `BinaryWriter` 7-bit encoded length prefix**.

| Field | Size | Description |
| --- | --- | --- |
| `packetSize` | 4 bytes (int32) | Total size of the datagram body |
| `subjectName` | variable | 7-bit length prefix + UTF-8 string |
| `frameIdx` | 4 bytes (int32) | Frame index |
| `boneCount` | 4 bytes (int32) | Number of bones |
| **Per bone** (repeated `boneCount` times): | | |
| `boneName` | variable | 7-bit length prefix + UTF-8 string |
| `localPosition` | 12 bytes (3 x float) | X, Y, Z position |
| `localRotation` | 16 bytes (4 x float) | X, Y, Z, W quaternion |
| `localScale` | 12 bytes (3 x float) | X, Y, Z scale |

> **Coordinate system:** the plugin converts from Unity's Y-up left-hand coordinate system (X, Y, Z)
> to Unreal's Z-up left-hand system (Z, X, Y) automatically.
