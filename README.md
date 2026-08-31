# MOVIN LiveLink Plugin V3 (MOVIN Studio v3.0.0+) for UE5

Receives real-time motion capture data from MOVIN Studio via UDP and feeds it into Unreal Engine's [LiveLink](https://dev.epicgames.com/documentation/en-us/unreal-engine/live-link-in-unreal-engine) system.

## Features

- Streams skeleton and animation data from MOVIN Studio over UDP
- Fits the retarget source mesh to the captured actor automatically, so driving your own character needs no manual calibration
- Detects skeleton changes when you switch characters
- Supports multiple characters, one MOVIN Studio instance and UDP port each
- Works in both the **Editor** and **Packaged Games**
- Configurable UDP port (default `11236`)

## Requirements

| | |
|---|---|
| Engine | Unreal Engine **5.3 - 5.8** |
| Platform | Windows 64-bit |
| Capture | MOVIN Studio **v3.0.0+** |
| Network | MOVIN Studio and Unreal on the same network |
| Project | A **C++ project** for Option 1; any project, including **Blueprint-only**, for Option 2 |

Unreal's own **LiveLink** plugin must be enabled as well: **Edit > Plugins**, search "Live Link", enable, restart.

## Installation

Pick one option.

### Option 1: From source (C++ project)

Copy this repository into your project's `Plugins/` directory as `MOVINLiveLink`:

```
YourProject/
|-- Content/
|-- Source/
|-- Plugins/
|   `-- MOVINLiveLink/      <-- copy here
|       |-- MOVINLiveLink.uplugin
|       |-- Source/
|       `-- Resources/
`-- YourProject.uproject
```

Reopen the project from the Epic Games Launcher. In most cases this triggers the plugin build and you are done.

If the plugin does not build automatically:

1. Right-click the `.uproject` file
2. Click **Show more options**
3. Click **Generate Visual Studio project files**
4. Open the generated Visual Studio solution
5. Right-click the project in Solution Explorer
6. Click **Build**
7. Reopen the project in Unreal Editor

### Option 2: Prebuilt package (Blueprint-only project)

A Blueprint-only project cannot compile the plugin, so use a prebuilt package instead.

1. Download the zip for your Unreal Engine version from the [prebuilt release](https://github.com/MOVIN3D/MOVIN-UELiveLink-Plugin-V3/releases/tag/prebuilt)
2. Extract the zip
3. Copy the extracted `MOVINLiveLinkPlugin` folder into your project's `Plugins/` folder
4. Reopen the project in Unreal Editor and enable **MOVINLiveLink** if prompted

The folder name does not have to match Option 1's. What matters is that `MOVINLiveLink.uplugin` sits directly inside it.

Packages cover Unreal Engine 5.3 - 5.8 on Windows 64-bit and are built from the current source. Debug symbols (`.pdb`) are not included.

## Quick Start

Do all of this once, whichever setup you are heading for.

### 1. Import the MOVINman mesh

Import `MOVINman_V3_Puppet_UE.fbx` into your project's Content. It ships with the plugin, so once you have installed it the file is already on disk:

| Installed with | Find the `.fbx` in |
|---|---|
| Option 1, from source | the root of this repository |
| Option 2, prebuilt package | `Plugins/MOVINLiveLinkPlugin/` in your project |

This is the mesh that **receives** an Actor stream. It is not the character anyone sees - if you go on to drive your own character it stays hidden, and only the retargeter reads it.

Skip this step if MOVIN Studio streams a Character rather than an Actor (path **B** below).

### 2. Add the LiveLink source

1. **Window > Virtual Production > Live Link**
2. **+ Source > MOVIN Live Source**
3. Leave the port at `11236` unless you have a reason to change it, and click **Add**

### 3. Point MOVIN Studio at Unreal

In MOVIN Studio, set:

- **Streaming Host** to the IP address of the machine running Unreal
- the streaming port to the same port as step 2

Both machines have to be on the same network. The same router or Wi-Fi is enough.

### 4. Start streaming

Start streaming from MOVIN Studio. A subject appears in the LiveLink panel, usually `MOVINMan` for an Actor stream. **Write the name down** - later steps need it exactly.

The status column reports the receive rate. Expected is `60 fps`; below `55` shows as a warning and below `50` as critical.

> **If no subject appears, stop here.** Check the UDP port through your firewall, the **Streaming Host** IP in MOVIN Studio, and that both machines are on the same network. Nothing further will work until a subject shows up.

## Choose your setup

| Your goal | Go to |
|---|---|
| Check that the capture is arriving | **A** below |
| Drive a character built from the same `.fbx` MOVIN Studio uses | **B** below |
| Drive your own character from an **Actor** stream | **C** below |
| ...onto a MetaHuman | [Retargeting an Actor Stream onto a MetaHuman](docs/metahuman-retargeting.md) |

### A. Check that the capture is arriving

1. Create an **Animation Blueprint** on `MOVINman_V3_Puppet_UE_Skeleton`
2. In the **AnimGraph**, add a **Live Link Pose** node and set its **Live Link Subject Name** to the subject from Quick Start step 4
3. Connect it to **Output Pose**, then compile and save
4. Drag `MOVINman_V3_Puppet_UE` into the level and set its **Anim Class** to that Animation Blueprint

The mesh should move now. It will also stretch or fold at some joints - that is expected, and [About the deforming MOVINman mesh](#about-the-deforming-movinman-mesh) explains why.

If it does not move, fix that before going any further. Everything below depends on it.

### B. Drive a character built from the same .fbx

When MOVIN Studio streams a **Character** rather than an Actor, the motion is already on that character's skeleton and no retargeting is involved. The mesh imported into Unreal must be the exact same `.fbx` MOVIN Studio uses.

1. Create an **Animation Blueprint** on your character's Skeleton
2. In the **AnimGraph**, add a **Live Link Pose** node and select the subject for that character
3. Connect it to **Output Pose**, then compile and save
4. Assign the Animation Blueprint to your Skeletal Mesh Actor

### C. Drive your own character from an Actor stream

An Actor stream lands on the MOVINman skeleton, so it reaches your character through an **IK Retargeter**. MOVINman stays in the scene as a hidden source; the retargeter reads its bone transforms and drives your character from them.

Complete **A** first - the retargeter has nothing to read until MOVINman is moving.

For a MetaHuman, follow [the MetaHuman guide](docs/metahuman-retargeting.md) instead. It covers this same ground plus the extra steps that character needs.

#### C.1 Source IK Rig

Right-click `MOVINman_V3_Puppet_UE` > **Create IK Rig**. In the IK Rig editor toolbar, run in order:

1. **Auto Create Retarget Chains**
2. **Auto Create IK**

#### C.2 Target IK Rig

Do exactly the same to your character's skeletal mesh: **Create IK Rig**, then the same two commands.

Running the same commands on both sides is what makes the chain names match, which is what makes the mapping in C.3 fill itself in. Do not author the chains by hand.

If your character has bones MOVINman does not - extra metacarpals, twist bones - **delete the chains covering them**. A chain with no counterpart on the source side has nothing to drive it and comes out wrong.

#### C.3 IK Retargeter

Right-click the source IK Rig > **Create IK Retargeter**, and set:

| Field | Value |
|---|---|
| Source IK Rig | the MOVINman rig from C.1 |
| Source Preview Mesh | `MOVINman_V3_Puppet_UE` |
| Target IK Rig | your character's rig from C.2 |
| Target Preview Mesh | your character's mesh |

Run **Auto-Map Chains**, then confirm the pelvis pair maps `Hips` to your character's pelvis bone.

#### C.4 Align the retarget poses

MOVINman imports in a T-pose and your character may not stand the same way. Retargeting measures everything against these poses, so they have to be made to agree first.

In the retargeter, switch to **Edit Pose**, select **Target**, and run **Auto Align All**. Check the viewport afterwards - both figures should be standing in the same shape - and nudge anything still off by hand.

> Align the two **reference** poses against each other, not against live motion. Adjusting this while streaming aligns the rig to whatever the actor happened to be doing at that moment.

#### C.5 Add MOVINman to your character Blueprint

Open your character Blueprint and add a **Skeletal Mesh Component** under the root:

| Setting | Value | If you skip it |
|---|---|---|
| Skeletal Mesh | `MOVINman_V3_Puppet_UE` | - |
| Anim Class | the Animation Blueprint from **A** | no pose arrives |
| Visible | off | two characters on screen |
| Visibility Based Anim Tick Option | `Always Tick Pose and Refresh Bones` | **freezes in T-pose the moment it is hidden** |

#### C.6 Retarget in your character's Animation Blueprint

Create an Animation Blueprint on your character's Skeleton. Its AnimGraph needs one node:

```
[Retarget Pose From Mesh] ---> [Output Pose]
```

| Node setting | Value |
|---|---|
| Retarget From | `Custom Skeletal Mesh Component` |
| IK Retargeter Asset | the retargeter from C.3 |
| Source Mesh Component | expose as a pin |
| LOD Threshold / IK LOD Threshold | `-1` |

> **Retarget From is the setting people miss.** Left at `Parent Skeletal Mesh Component`, the node looks for a source among the component's attach parents, finds nothing, and outputs the reference pose. Nothing appears to happen and nothing errors.

Add a **Skeletal Mesh Component** variable named `SourceMesh`, connect it to the node's **Source Mesh Component** pin, then compile and save.

Select your character's mesh component and set its **Anim Class** to this Animation Blueprint.

#### C.7 Connect the source at runtime

In your character Blueprint's **Event Graph**, build one unbroken execution line off **Event BeginPlay**:

```
Event BeginPlay -> Cast To <your AnimBP> -> SET Source Mesh -> Add Tick Prerequisite Component
```

1. Drag your character's mesh component in from the **Components** panel, then drag off it > **Get Anim Instance** > **Cast To** your Animation Blueprint from C.6
2. Drag from the cast's **As ...** output pin > **Set Source Mesh**, and connect your MOVINman component to its **Source Mesh** pin
3. Drag from your character's mesh component > **Add Tick Prerequisite Component**, and connect your MOVINman component to **Prerequisite Component**. This makes MOVINman tick first; without it the motion lags a frame
4. Compile

Build these by dragging off pins rather than placing nodes from the right-click menu - that way the editor wires the pin that matters for you. Two things to check before you compile:

- The **Add Tick Prerequisite Component** node must read **Target is Actor Component**. If it reads `Target is Actor`, you added the wrong overload - delete it and start the drag from the mesh component.
- Every node must sit on the execution line. A cast left off to the side still compiles, and then the SET runs with nothing in its Target.

Place your character in the level and start streaming. To preview in the editor viewport rather than in PIE, enable **Update Animation In Editor** on both skeletal mesh components.

## About the deforming MOVINman mesh

**When streaming an Actor, expect the MOVINman mesh to deform. This is correct behaviour, not a defect.**

MOVIN Studio calibrates the skeleton to each actor's body, so an Actor stream carries bone lengths that belong to the actor rather than to the `MOVINman_V3_Puppet_UE` mesh. Those lengths are applied verbatim, which is what keeps the motion data intact - but it means joints whose calibrated length differs from the mesh visibly change shape. A shortened upper arm, for example, pulls the forearm up into it and the elbow appears to bulge.

The plugin reports the figures in an editor notification and in the Output Log:

```
Skeleton Calibration Offset - 'MOVINMan'
Head 1.63x, Neck 1.49x, Spine 0.62x, Arm 0.87x.
```

**If you are retargeting (path C), there is nothing for you to do about this.** Once a subject has streamed enough frames to be calibrated - roughly half a second, and the actor has to have moved - the plugin gives the source component a copy of its mesh whose reference pose carries the actor's bone lengths, so every measurement the retargeter takes is correct for that actor. No scale factor to tune, no changes to your IK Rig or IK Retargeter, and the mesh asset on disk is never modified. A recalibration, including the brief one MOVIN Studio performs when a stream reconnects, is picked up on the next frame.

That fitted mesh is built to be measured, not rendered. Its skin weights were painted against the original bind pose, so **it looks wrong if you make the source component visible** - collapsed or spiky around the joints that were refitted. That is the fitted mesh doing its job rather than a defect, and it never reaches the character being driven. Keeping the source component hidden, which C.5 does anyway, is all that is needed.

To turn fitting off and keep the mesh's authored reference pose:

```
movin.ActorMesh.AutoFit 0
```

For manual control there are two Blueprint nodes: **Fit Mesh To MOVIN Actor** (Skeletal Mesh Component, subject name) and **Is MOVIN Actor Calibrated** (subject name).

> The **Translation Retargeting** settings on a Skeleton asset are not worth trying here - they have no effect on Live Link. They are only consulted on the AnimSequence and PoseAsset paths.

## Packaged Game Setup

The plugin runs in packaged (shipped) games. A LiveLink source cannot be saved into a build the way it is added in the editor, so create it at runtime with the **Add MOVIN LiveLink Source** Blueprint node.

### 1. Enable the plugins

Confirm both are enabled in **Edit > Plugins**: search "Live Link", then search "MOVIN".

### 2. Call the Blueprint node at startup

#### Option A: Level Blueprint (simplest)

1. Open your level in the editor
2. Click **Blueprints** in the toolbar > **Open Level Blueprint**
3. Right-click in the graph > add an **Event BeginPlay** node
4. Drag from the BeginPlay execution pin > search and add **Add MOVIN LiveLink Source**
5. Set the **Port** parameter (default `11236`)
6. **Compile** and **Save**

```
[Event BeginPlay] ---> [Add MOVIN LiveLink Source]
                            Port: 11236
```

> **Note:** the source is re-created each time the level loads. If your game has multiple levels, add the node to each level's Blueprint.

#### Option B: GameInstance Blueprint (recommended)

Use this if you want the source to persist across level changes:

1. Content Browser > right-click > **Blueprint Class** > search **GameInstance** > create it (e.g. `BP_MyGameInstance`)
2. Open it > in the **Event Graph**, add an **Event Init** node
3. Drag from it > add **Add MOVIN LiveLink Source** (port `11236`)
4. **Compile** and **Save**
5. **Edit > Project Settings > Maps & Modes > Game Instance Class** > set it to your `BP_MyGameInstance`

This runs once at game startup and survives level transitions.

### 3. Package

Package your game normally, and make sure MOVIN Studio is streaming to the configured port when the game runs.

## Multiple Characters

To stream multiple characters at the same time, use **one Tracin device and one MOVIN Studio instance per character**.

1. In Unreal, create one **MOVIN Live Source** per character, each on its own UDP port - `11236`, `11237`, and so on
2. On each computer running Tracin, set that MOVIN Studio instance's streaming port to match one of those sources
3. Set **Streaming Host** on every instance to the IP address of the computer running Unreal

Each source then receives one character stream on its own port. All the computers involved have to be on the same network.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| No subject in the LiveLink panel | Port blocked, wrong Streaming Host, or different networks | Quick Start 3 |
| Subject appears but MOVINman does not move | Live Link Pose subject name does not match | A.2 |
| MOVINman stretches or folds at joints | Expected on an Actor stream | [About the deforming MOVINman mesh](#about-the-deforming-movinman-mesh) |
| Your character is frozen in T-pose | Hidden MOVINman still on the default anim tick option | `Always Tick Pose and Refresh Bones` (C.5) |
| Your character does not move at all | Retarget node left on `Parent Skeletal Mesh Component` | Switch to `Custom` and connect the pin (C.6) |
| Compile error: requires a source Skeletal Mesh Component | The Source Mesh Component pin is empty | C.6, C.7 |
| Your character stands with bent knees | Actor fitting has not run yet - no movement, or too few frames | Have the actor move for a few seconds, then look for the `fitted` line in the log |
| Arms splayed or twisted | Retarget poses not aligned | **Auto Align All** (C.4) |
| Fingers do not follow | Finger chains unmapped, or finger streaming off in MOVIN Studio | Check the chain mapping, then the Studio setting |
| Motion lags one frame | Tick prerequisite missing | **Add Tick Prerequisite Component** (C.7) |
| Feet slide | IK chains disabled, or no ground contact settings | IK Chains, Speed Planting and Floor Constraint in the retargeter |
| Receive rate below 60 fps | Network or sender rate | Prefer a wired connection, and check for competing traffic |

## Logging

The plugin logs under the `LogMOVINLiveLink` category. For diagnostic detail in the Output Log:

```
Log LogMOVINLiveLink Verbose
```

Key events:

- New subject detected (first packet for a character)
- Skeleton changes (bone count or bone name changes)
- Parse failures and invalid datagrams
- Skeleton calibration offset between an Actor subject and the Skeletal Mesh it drives
- Actor fitting applied to a source mesh, with the calibration revision it was built for

## Reference

- [Retargeting an Actor Stream onto a MetaHuman](docs/metahuman-retargeting.md) - full walkthrough for that character
- [UDP Packet Format](docs/protocol.md) - wire format, for writing a sender or debugging at the packet level

## License

Copyright 2025 MOVIN. All Rights Reserved.
