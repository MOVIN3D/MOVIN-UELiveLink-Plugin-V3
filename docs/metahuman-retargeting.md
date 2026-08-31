# Retargeting an Actor Stream onto a MetaHuman

How to drive a MetaHuman with an Actor stream from MOVIN Studio, from an empty project to a
MetaHuman moving in the viewport.

The short version: MOVINman receives the stream on a hidden component, an IK Retargeter copies that
motion onto the MetaHuman body, and the plugin fits the MOVINman mesh to the actor so the
retargeting comes out correct. Every step below is one of those three things.

> If MOVIN Studio is streaming a **Character** rather than an Actor, none of this applies. Import the
> exact same `.fbx` used in MOVIN Studio and drive it with a Live Link Pose node directly.

## Before you start

| | |
|---|---|
| Engine | Unreal Engine 5.3 - 5.8, Windows 64-bit |
| Capture | MOVIN Studio v3.0.0+ streaming an **Actor** |
| Plugin | MOVINLiveLink 1.1.0 or newer, with **LiveLink** enabled |
| Character | A MetaHuman already imported into the project |
| Source mesh | `MOVINman_V3_Puppet_UE.fbx`, included in this repository |
| Network | MOVIN Studio and Unreal on the same network |

---

## 1. Get the stream into Unreal

### 1.1 Install the plugin

Copy the plugin into your project's `Plugins/` folder and reopen the editor. In **Edit > Plugins**,
confirm both **LiveLink** and **MOVINLiveLink** are enabled.

### 1.2 Import the MOVINman mesh

Import `MOVINman_V3_Puppet_UE.fbx`. This is the mesh that *receives* the capture. It is not the
character anyone sees - it is an intermediate the retargeter reads from.

Its bones use Mixamo-style names: `Hips`, `Spine1` - `Spine3`, `LeftUpLeg`, `LeftShoulder`.

### 1.3 Add the LiveLink source

**Window > Virtual Production > Live Link** > **+ Source** > **MOVIN Live Source**. Leave the port at
`11236` unless you have a reason to change it.

In MOVIN Studio, set **Streaming Host** to the IP of the machine running Unreal, and the streaming
port to match.

### 1.4 Start streaming and note the subject name

Start streaming. A subject appears in the LiveLink panel - usually `MOVINMan` for an Actor stream.
**Write the name down**; the next step needs it exactly.

The source status column reports the receive rate. Expected is `60 fps`; below `55` shows as a
warning and below `50` as critical.

> If no subject appears, stop here. Check the UDP port through your firewall, the **Streaming Host**
> IP in MOVIN Studio, and that both machines are on the same network. Nothing downstream will work
> until a subject shows up.

### 1.5 Animation Blueprint for MOVINman

Create an Animation Blueprint on `MOVINman_V3_Puppet_UE_Skeleton`. The AnimGraph is two nodes:

```
[Live Link Pose] ---> [Output Pose]
```

Set **Live Link Subject Name** on the Live Link Pose node to the name from 1.4.

> **Checkpoint.** Drop MOVINman into the level with this Animation Blueprint assigned. It should move
> right now. If it does not, fix that before continuing - everything after this depends on it.
>
> The mesh stretching or folding at some joints is expected. Section 2 explains why.

---

## 2. Why the mesh deforms, and what the plugin does about it

MOVIN Studio calibrates the skeleton to each actor's body, so an Actor stream carries bone
lengths that belong to the actor rather than to the MOVINman mesh. Those lengths are applied
verbatim, which is what keeps the motion data intact, but joints whose calibrated length differs from
the mesh visibly change shape.

The plugin reports the actual figures in an editor notification and in the Output Log:

```
Skeleton Calibration Offset - 'MOVINMan'
Neck1 0.44x, LeftFoot 0.79x, LeftLeg 0.79x, Neck 1.21x, ...
```

This matters for retargeting because an IK Retargeter measures how far a limb is extended, and how
high the pelvis sits, **against the reference pose of the source mesh**. An actor whose legs
measure 0.79x streams a straight leg that reads as only 79% extended, so the retargeted MetaHuman
stands with bent knees.

From 1.1.0 the plugin handles this. Once a subject has streamed enough frames to be calibrated -
roughly half a second, and the actor has to have moved - it gives the source component a copy of
its mesh whose reference pose carries the actor's bone lengths. Every ratio the retargeter works
from is then correct by construction, with no scale factor to tune and no changes to your IK Rig or
IK Retargeter. The asset on disk is not modified.

If MOVIN Studio recalibrates - which it also does briefly whenever a stream stops and reconnects -
the mesh is refitted on the next frame.

To turn it off: `movin.ActorMesh.AutoFit 0`.

---

## 3. Build the IK Rigs and the Retargeter

You build these once and reuse them for every character.

Do not author the chains by hand. The IK Rig editor can derive them from the skeleton, and running
the same two commands on both rigs is what makes the chain names match - which is what makes the
mapping in 3.3 fill itself in.

### 3.1 Source IK Rig - MOVINman

Right-click the MOVINman skeletal mesh > **Create IK Rig**. In the IK Rig editor toolbar, run:

1. **Auto Create Retarget Chains** - compares the skeleton against known templates and splits it into
   chains, and sets the retarget root to `Hips`
2. **Auto Create IK** - adds a Full-Body IK setup with goals on the feet and hands

That produces 21 chains and 4 goals. Left side shown; the right side mirrors it exactly.

| Chain | Start bone | End bone | Goal |
|---|---|---|---|
| `Spine` | `Spine` | `Spine3` | |
| `Neck` | `Neck` | `Neck` | |
| `Head` | `Head` | `Head` | |
| `LeftClavicle` | `LeftShoulder` | `LeftShoulder` | |
| `LeftArm` | `LeftArm` | `LeftHand` | `LeftHandIK` |
| `LeftLeg` | `LeftUpLeg` | `LeftFoot` | `LeftFootIK` |
| `LeftFoot` | `LeftToeBase` | `LeftToeBase` | |
| `LeftThumb` | `LeftHandThumb1` | `LeftHandThumb3` | |
| `LeftIndex` | `LeftHandIndex1` | `LeftHandIndex3` | |
| `LeftMiddle` | `LeftHandMiddle1` | `LeftHandMiddle3` | |
| `LeftRing` | `LeftHandRing1` | `LeftHandRing3` | |
| `LeftPinky` | `LeftHandPinky1` | `LeftHandPinky3` | |

Goals: `LeftFootIK` on `LeftFoot`, `LeftHandIK` on `LeftHand`, and the two right-side equivalents.

Two of these look wrong at a glance and are not. `Neck` covers only the `Neck` bone - `Neck1` is left
out of every chain. And `LeftFoot` is the *toe* chain, on `LeftToeBase`; the ankle belongs to
`LeftLeg`.

### 3.2 Target IK Rig - MetaHuman body

Do the same thing to the MetaHuman body mesh: right-click it > **Create IK Rig**, then
**Auto Create Retarget Chains** and **Auto Create IK**.

The same commands on a MetaHuman skeleton produce the same chain names over MetaHuman bones, which is
the whole point - `Spine` becomes `spine_01` to `spine_05`, `LeftLeg` becomes `thigh_l` to `foot_l`,
`LeftFoot` becomes `ball_l`, and the retarget root becomes `pelvis`.

**Then delete the eight `*Metacarpal` chains** - `LeftIndexMetacarpal`, `LeftMiddleMetacarpal`,
`LeftRingMetacarpal`, `LeftPinkyMetacarpal` and the four right-side ones. A MetaHuman hand has
metacarpal bones and the MOVINman skeleton does not, so there is nothing to drive them from. Left in
place they make the retargeted hands come out wrong. Deleting them leaves the 21 chains that have a
counterpart on both sides.

> A MetaHuman ships with an IK Rig already, at
> `Content/MetaHumans/Common/Common/Animation/Retargeting/IK_MetaHuman_<body type>`. **Do not use it
> here.** It names its chains differently - `head` and `root` in lower case, separate twist and
> metacarpal chains, goals called `hand_l_Goal` rather than `LeftHandIK` - so it will not auto-map
> against the MOVINman rig. Build your own with the two commands above.

### 3.3 IK Retargeter

Right-click the source IK Rig > **Create IK Retargeter**.

| Field | Value |
|---|---|
| Source IK Rig | the MOVINman rig from 3.1 |
| Source Preview Mesh | `MOVINman_V3_Puppet_UE` |
| Target IK Rig | the MetaHuman rig from 3.2 |
| Target Preview Mesh | your MetaHuman body mesh |

Run **Auto-Map Chains**. Both rigs came out of the same commands, so all 21 chains pair up by name
with nothing left over. Confirm the pelvis pair reads `Hips` to `pelvis` and you are done.

### 3.4 Retarget pose

The two skeletons do not stand the same way out of the box: MOVINman imports in a T-pose and a
MetaHuman's reference pose is an A-pose. Retargeting measures everything against these poses, so they
have to be made to agree first.

In the retargeter, switch to **Edit Pose**, select the **Target**, and run **Auto Align All**. The
MetaHuman arms swing out to match MOVINman's T-pose. Check the viewport afterwards - both figures
should be standing in the same shape - and nudge the shoulders or arms by hand if anything is still
off.

> Align the two **reference** poses against each other, not against live motion. Adjusting this while
> streaming aligns the rig to whatever the actor happened to be doing at that moment.

---

## 4. Wire up the MetaHuman

> **The MetaHuman Blueprint has a `Use Live Link Body` switch. It will not work here.** It is worth
> knowing why before you go looking for a shortcut.
>
> Turning it on points the Body component at the character's own retargeting Animation Blueprint, and
> that Blueprint retargets through `RTG_MetaHuman_<body type>` - whose source and target IK Rig are
> **the same rig**, over the same MetaHuman preview body. It exists to move a stream that is already
> in MetaHuman skeleton space onto this particular character's proportions.
>
> Its companion `Live Link Body Retarget` slot takes a `LiveLinkRetargetAsset`, which renames bones;
> it is not an IK Retargeter and cannot reconcile two different rigs. MOVIN streams Mixamo-style bone
> names and orientations, so no amount of renaming makes it land on a MetaHuman skeleton. That
> reconciliation is exactly what the IK Retargeter in section 3 does, which is why the wiring below
> is done by hand.

### 4.1 Create a child Blueprint

Right-click `BP_<name>` > **Create Child Blueprint Class** and work there. The MetaHuman Blueprint is
regenerated when the character is reassembled, which would discard edits made directly to it.

### 4.2 Add MOVINman as a child component

Add a **Skeletal Mesh Component** under `Root` - not under `Body` or `Face`.

| Setting | Value | If you skip it |
|---|---|---|
| Skeletal Mesh | `MOVINman_V3_Puppet_UE` | - |
| Anim Class | the Animation Blueprint from 1.5 | no pose arrives |
| Visible | off | two characters on screen |
| Visibility Based Anim Tick Option | `Always Tick Pose and Refresh Bones` | **freezes in T-pose the moment it is hidden** |

Keeping it as a child of the same actor makes the tick order in 4.4 straightforward. A separate actor
works too but can lag by a frame.

### 4.3 Animation Blueprint for the MetaHuman body

Find the body skeletal mesh - it is the one on the `Body` component of the MetaHuman Blueprint -
right-click it, and choose **Create** > **Anim Blueprint**. It is named after the skeleton rather
than the character, so you get `metahuman_base_skel_AnimBlueprint`, created next to the MetaHuman
common content. Move it beside your character, e.g. `MetaHumans/<name>/Body/`.

Its AnimGraph needs one node:

```
[Retarget Pose From Mesh] ---> [Output Pose]
```

| Node setting | Value |
|---|---|
| Retarget From | `Custom Skeletal Mesh Component` |
| IK Retargeter Asset | the retargeter from 3.3 |
| Source Mesh Component | exposed as a pin, driven by a variable (4.4) |
| LOD Threshold / IK LOD Threshold | `-1` |
| Suppress Warnings | leave unchecked |

> **Retarget From is the setting people miss.** Left at `Parent Skeletal Mesh Component`, the node
> walks up the target component's *attach parents* looking for a source. The MetaHuman `Body` is the
> root component, so there is nothing above it, the source stays null, and the node outputs the
> reference pose. Nothing appears to happen and nothing errors.

Add a **Skeletal Mesh Component** variable to the Animation Blueprint - `SourceMesh` - and connect it
to the node's `Source Mesh Component` pin.

### 4.4 Event graph in the child Blueprint

This goes in the **child Blueprint**, not the Animation Blueprint. Open it and switch to the
**Event Graph** tab.

The finished graph is one unbroken execution line:

```
Event BeginPlay -> Parent: BeginPlay -> Cast To <AnimBP> -> SET Source Mesh -> Add Tick Prerequisite Component
```

Build it by dragging off pins rather than placing nodes from the right-click menu. Dragging makes the
editor connect the pin that matters for you, and every mistake in this section comes from a
connection that was made by hand or left at its default.

**Start the execution line**

1. If the graph has no `Event BeginPlay`, right-click on empty graph space, search `Event BeginPlay`,
   and add it.
2. Right-click the `Event BeginPlay` node > **Add Call to Parent Function**. This adds
   `Parent: BeginPlay` already connected. If you have both nodes but no wire between them, drag from
   the `Event BeginPlay` execution pin (the white triangle on its right edge) onto the left execution
   pin of `Parent: BeginPlay`.

**Bring the two components into the graph**

3. In the **Components** panel (top left), drag **Body** into the graph and drop it. You get a small
   blue `Body` node. Inherited components are dragged from this panel, not from My Blueprint.
4. Drag your MOVINman component in the same way. You now have two component nodes to pull from.

**Reach the Animation Blueprint instance**

5. Drag from the blue output pin on the right of the `Body` node, release on empty space, search
   `Get Anim Instance`, and add it. `Body` connects itself to the node's `Target`.
6. Drag from `Get Anim Instance` > `Return Value`, release, and search for the name of your body
   Animation Blueprint from 4.3. Choose **Cast To \<that name\>**. `Return Value` connects itself to
   the cast's `Object` pin.
   - Not in the list? Compile and save the Animation Blueprint first, then uncheck **Context
     Sensitive** in the top right of the search menu and search again.
7. Drag from the `Parent: BeginPlay` execution pin onto the cast's left execution pin, so the cast is
   on the execution line.

**Set the source mesh**

8. Drag from the cast's **As \<name\>** output pin - the blue one below `Cast Failed` - release, and
   search `Set Source Mesh`. The SET node appears with its **Target** already wired to the cast
   output. This is the whole reason to drag from that pin: a SET node placed any other way defaults
   its Target to `self` and fails to compile, because `self` is the actor and the variable lives on
   the Animation Blueprint.
9. Drag from the cast's right execution pin onto the SET node's left execution pin.
10. Drag from your MOVINman component node's output pin onto the SET node's **Source Mesh** value pin.

**Fix the tick order**

11. Drag from the `Body` node's output pin, release on empty space, and search
    `Add Tick Prerequisite Component`. Add it. `Body` connects itself to `Target`.
12. **Check the node's subtitle.** It must read **Target is Actor Component**, and the `Target` pin
    must show `Body`. If it reads `Target is Actor` with `Target` set to `self`, you added the actor
    overload - delete the node and repeat step 11, starting the drag from the `Body` node.
13. Drag from your MOVINman component node's output pin onto the **Prerequisite Component** pin.
14. Drag from the SET node's right execution pin onto this node's left execution pin.

**Check it before compiling**

15. Follow the white execution wire with your eye. It must pass through all five nodes in order,
    with no node sitting off to the side unconnected. A cast that is not on the execution line still
    compiles, and then the SET silently runs with nothing in its Target.
16. Compile. A clean compile means the wiring is legal, not that it is right - step 15 is the check
    that matters.

Two more things worth stating plainly:

- The chain must run **after `Parent: BeginPlay`**. The parent can replace the Body anim instance
  during its own BeginPlay, which discards a variable set before it.
- Do not use `Try Get Pawn Owner` anywhere here. The MetaHuman Blueprint's parent class is `Actor`,
  so it always returns null. Use `Get Owning Actor` if you need the owner.

### 4.5 Assign the Anim Class

Select the **Body** component in the child Blueprint and set **Anim Class** to the Animation
Blueprint from 4.3.

Leave the Post Process Anim BP (`ABP_Body_PostProcess`) alone - RigLogic and the head IK correction
live there and run after the retargeted pose.

> **If you also use facial LiveLink:** when a face LiveLink subject is set - the **ARKit Face Subj**
> variable on the MetaHuman Blueprint - the Blueprint calls `SetAnimInstanceClass` on Body during
> BeginPlay and overwrites the Anim Class you just set. For body only, leave that subject empty. For
> both, duplicate the Animation Blueprint the character uses for facial LiveLink, put the Retarget
> Pose From Mesh node into it as the body pose, and point the Blueprint at your duplicate.

---

## 5. Run it

Place the child Blueprint in the level and start streaming. To preview in the editor viewport rather
than in PIE, enable **Update Animation In Editor** on both skeletal mesh components.

### What the log should say

```
Subject 'MOVINMan': First time seen - registering MOVIN skeleton with 55 bones

[Skeleton Calibration Offset] Subject 'MOVINMan' is streaming bone lengths calibrated to the
actor, ... Neck1 0.44x, LeftFoot 0.79x, LeftLeg 0.79x, ...

Subject 'MOVINMan': fitted 'MOVINman_V3_Puppet_UE' to the streamed actor (calibration revision 9)
Subject 'MOVINMan': streamed bone lengths match Skeletal Mesh 'MOVINman_V3_Puppet_UE_Actor_0'
(53 bones compared).
```

The last line is the one that matters. The same check that reported a mismatch now reports a match,
which means every ratio the retargeter measures is correct for this actor.

If you see nothing, run `Log LogMOVINLiveLink Verbose` in the console.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| MetaHuman frozen in T-pose | Hidden MOVINman still on the default anim tick option | `Always Tick Pose and Refresh Bones` (4.2) |
| Nothing happens at all | Retarget node left on `Parent Skeletal Mesh Component` | Switch to `Custom` and connect the pin (4.3) |
| Compile error, `requires a source Skeletal Mesh Component` | The pin is empty | Connect or bind the variable (4.3) |
| Cast Failed | Body is running a different Animation Blueprint | Check 4.5, and whether a face subject overwrote it |
| Stands with bent knees | Actor fitting has not run - no movement yet, or too few frames | Have the actor move for a few seconds, then look for the `fitted` line |
| Arms splayed or twisted | Retarget poses not aligned | **Auto-Align All** (3.4) |
| Fingers do not follow | Finger chains unmapped, or finger streaming off in MOVIN Studio | Check the chain mapping, then the Studio setting |
| Motion lags one frame | Tick prerequisite missing | `Add Tick Prerequisite Component` (4.4) |
| Feet slide | IK chains disabled, or no ground contact settings | IK Chains, Speed Planting and Floor Constraint in the retargeter |
| Receive rate reported low | Network or sender rate | Expected is 60 fps. Check for a wired connection and competing traffic |

## Face animation

This covers the MetaHuman **body**. Facial capture uses the MetaHuman facial LiveLink path unchanged:
the Face component copies pose and curves from Body and feeds RigLogic, so it does not conflict with
body retargeting. See the note in 4.5 for the one place the two interact.
