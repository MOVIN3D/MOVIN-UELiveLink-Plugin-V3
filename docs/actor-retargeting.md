# Retarget an Actor Stream

An Actor stream lands on the MOVINman skeleton, so it reaches your character through an **IK Retargeter**. MOVINman stays in the scene as a hidden source; the retargeter reads its bone transforms and drives your character from them.

Complete [Apply the Stream](../README.md#apply-the-stream) for MOVINman first - the retargeter has nothing to read until MOVINman is moving.

For a MetaHuman, follow [the MetaHuman guide](metahuman-retargeting.md) instead. It covers this same ground plus the extra steps that character needs.

## 1. Source IK Rig

Right-click `MOVINman_V3_Puppet_UE` > **Create IK Rig**. In the IK Rig editor toolbar, run in order:

1. **Auto Create Retarget Chains**
2. **Auto Create IK**

## 2. Target IK Rig

Do exactly the same to your character's skeletal mesh: **Create IK Rig**, then the same two commands.

Running the same commands on both sides is what makes the chain names match, which is what makes the mapping in step 3 fill itself in. Do not author the chains by hand.

If your character has bones MOVINman does not - extra metacarpals, twist bones - **delete the chains covering them**. A chain with no counterpart on the source side has nothing to drive it and comes out wrong.

## 3. IK Retargeter

Right-click the source IK Rig > **Create IK Retargeter**, and set:

| Field | Value |
|---|---|
| Source IK Rig | the MOVINman rig from step 1 |
| Source Preview Mesh | `MOVINman_V3_Puppet_UE` |
| Target IK Rig | your character's rig from step 2 |
| Target Preview Mesh | your character's mesh |

Run **Auto-Map Chains**, then confirm the pelvis pair maps `Hips` to your character's pelvis bone.

## 4. Align the retarget poses

MOVINman imports in a T-pose and your character may not stand the same way. Retargeting measures everything against these poses, so they have to be made to agree first.

In the retargeter, switch to **Edit Pose**, select **Target**, and run **Auto Align All**. Check the viewport afterwards - both figures should be standing in the same shape - and nudge anything still off by hand.

> Align the two **reference** poses against each other, not against live motion. Adjusting this while streaming aligns the rig to whatever the actor happened to be doing at that moment.

## 5. Add MOVINman to your character Blueprint

Open your character Blueprint and add a **Skeletal Mesh Component** under the root:

| Setting | Value | If you skip it |
|---|---|---|
| Skeletal Mesh | `MOVINman_V3_Puppet_UE` | - |
| Anim Class | the MOVINman Animation Blueprint from the README | no pose arrives |
| Visible | off | two characters on screen |
| Visibility Based Anim Tick Option | `Always Tick Pose and Refresh Bones` | **freezes in T-pose the moment it is hidden** |

## 6. Retarget in your character's Animation Blueprint

Create an Animation Blueprint on your character's Skeleton. Its AnimGraph needs one node:

```
[Retarget Pose From Mesh] ---> [Output Pose]
```

| Node setting | Value |
|---|---|
| Retarget From | `Custom Skeletal Mesh Component` |
| IK Retargeter Asset | the retargeter from step 3 |
| Source Mesh Component | expose as a pin |
| LOD Threshold / IK LOD Threshold | `-1` |

> **Retarget From is the setting people miss.** Left at `Parent Skeletal Mesh Component`, the node looks for a source among the component's attach parents, finds nothing, and outputs the reference pose. Nothing appears to happen and nothing errors.

Add a **Skeletal Mesh Component** variable named `SourceMesh`, connect it to the node's **Source Mesh Component** pin, then compile and save.

Select your character's mesh component and set its **Anim Class** to this Animation Blueprint.

## 7. Connect the source at runtime

In your character Blueprint's **Event Graph**, build one unbroken execution line off **Event BeginPlay**:

```
Event BeginPlay -> Cast To <your AnimBP> -> SET Source Mesh -> Add Tick Prerequisite Component
```

1. Drag your character's mesh component in from the **Components** panel, then drag off it > **Get Anim Instance** > **Cast To** your Animation Blueprint from step 6
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

Bone-length differences are recorded in the Output Log as `[Skeleton Calibration Offset]`.
No editor popup is shown for these expected differences.

**When retargeting, there is nothing for you to do about this.** Once a subject has streamed enough frames to be calibrated - roughly half a second, and the actor has to have moved - the plugin gives the source component a copy of its mesh whose reference pose carries the actor's bone lengths, so every measurement the retargeter takes is correct for that actor. No scale factor to tune, no changes to your IK Rig or IK Retargeter, and the mesh asset on disk is never modified. A recalibration, including the brief one MOVIN Studio performs when a stream reconnects, is picked up on the next frame.

That fitted mesh is built to be measured, not rendered. Its skin weights were painted against the original bind pose, so **it looks wrong if you make the source component visible** - collapsed or spiky around the joints that were refitted. That is the fitted mesh doing its job rather than a defect, and it never reaches the character being driven. Keeping the source component hidden, which step 5 does anyway, is all that is needed.

To turn fitting off and keep the mesh's authored reference pose:

```
movin.ActorMesh.AutoFit 0
```

For manual control there are two Blueprint nodes: **Fit Mesh To MOVIN Actor** (Skeletal Mesh Component, subject name) and **Is MOVIN Actor Calibrated** (subject name).

> The **Translation Retargeting** settings on a Skeleton asset are not worth trying here - they have no effect on Live Link. They are only consulted on the AnimSequence and PoseAsset paths.

