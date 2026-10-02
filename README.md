# MOVIN LiveLink Plugin v3.3.0 for UE5

Stream motion from MOVIN Studio into Unreal Engine through LiveLink, using an Actor
or a character loaded in Studio.

## Requirements

- **Unreal Engine 5.3–5.8**, Windows 64-bit. Download the package for your engine version.
- Enable Unreal's **LiveLink** and **MOVINLiveLink** plugins under **Edit > Plugins**.
- Use a C++ project when building the plugin from source or shipping a packaged game.

| MOVIN Studio | Recommended plugin |
|---|---|
| v3.3.0 or later | [v3.3.0](https://github.com/MOVIN3D/MOVIN-UELiveLink-Plugin-V3/releases/tag/v3.3.0) |
| v3.0.0–v3.2.0 | [v3.0.0](https://github.com/MOVIN3D/MOVIN-UELiveLink-Plugin-V3/releases/tag/v3.0.0) |

Studio's connection status and LiveLink FPS display require Studio and plugin v3.3.0+.

## Installation

1. Download the ZIP matching your Unreal Engine version from the release above.
2. Extract it and copy the `MOVINLiveLinkPlugin` folder into your project's `Plugins/`
   folder. `MOVINLiveLink.uplugin` must be directly inside that folder.
3. Open the project, enable **LiveLink** and **MOVINLiveLink**, and restart if prompted.

Prebuilt packages support Blueprint-only projects in the Editor. For packaged games,
use a C++ project and follow [Packaged Game Setup](docs/packaged-games.md).

To install from source, copy this repository into `Plugins/MOVINLiveLink` in a C++
project, generate the project's Visual Studio files and build the project.

To update, close Unreal, back up the installed plugin folder and replace it with the
new package for the same engine version. Keep only one copy of the plugin in the project.

## Quick Start

1. Open **Window > Virtual Production > Live Link**.
2. Choose **+ Source > MOVIN Live Source**, set port **11236**, and click **Add**.
3. In Studio, select **Unreal LiveLink** and the Actor or Character to stream.
4. Set the destination to the Unreal computer's IPv4 address, or `127.0.0.1` when
   both apps run on the same computer. Set the same port as the LiveLink source.
5. Click **Start Streaming** in Studio. Note the Subject name that appears in LiveLink;
   an Actor stream normally uses `MOVINMan`.

## Apply the Stream

For an **Actor** stream, import the included `MOVINman_V3_Puppet_UE.fbx`.
For a **Character** stream, import the exact same `.fbx` used in Studio.

1. Create an **Animation Blueprint** for that mesh's Skeleton.
2. Add a **Live Link Pose** node to its AnimGraph and set **Live Link Subject Name**
   to the Subject shown in the LiveLink panel.
3. Connect it to **Output Pose**, compile and save.
4. Place the Skeletal Mesh in the level and assign that Animation Blueprint as its
   **Anim Class**.

To drive a different character from an Actor stream, follow the
[Actor retargeting guide](docs/actor-retargeting.md), or the
[MetaHuman guide](docs/metahuman-retargeting.md).

## Actor Skeleton and Retargeting

Studio sends the Actor's calibrated bone lengths, and Unreal applies that actual
skeleton. The MOVINman preview can therefore stretch or fold where its original
proportions differ from the Actor.

During retargeting, the plugin automatically fits the source skeleton to the Actor.
Keep the MOVINman source mesh hidden and set its **Visibility Based Anim Tick Option**
to **Always Tick Pose and Refresh Bones** so it continues driving your character.
No manual bone-length adjustment is needed.

## Streaming Status

Studio shows the plugin connection, the source name and bone count, and **LiveLink FPS**.
LiveLink FPS counts motion frames delivered to LiveLink. Unknown values appear as `-`.

Allow the motion port (**UDP 11236** by default) into Unreal and status replies on
**UDP 39581** into Studio when using separate computers.

For simultaneous characters, use a separate Studio instance and LiveLink source
per character, with distinct UDP ports and Subject names.

## Troubleshooting

| Problem | Check |
|---|---|
| No LiveLink Subject | Check the destination IP, matching UDP port and firewall. |
| Subject appears but the mesh does not move | Match the Subject name in `Live Link Pose` and assign the correct Anim Class. |
| Character freezes when MOVINman is hidden | Set the source mesh to `Always Tick Pose and Refresh Bones`. |
| Retargeted character stays in its reference pose | Set `Retarget From` to `Custom Skeletal Mesh Component` and connect the source mesh; see the retargeting guide. |
| Arms are twisted or splayed | Align the source and target retarget poses. |
| Fingers do not move | Enable Hand streaming in Studio and check finger-chain mapping. |
| Plugin is absent in a packaged game | Use a C++ project and create the LiveLink source at startup; see Packaged Game Setup. |

## License

Copyright 2025 MOVIN. All Rights Reserved.
