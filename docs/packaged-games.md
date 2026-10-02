# Packaged Game Setup

The plugin runs in packaged (shipped) games. A LiveLink source cannot be saved into a build the way it is added in the editor, so create it at runtime with the **Add MOVIN LiveLink Source** Blueprint node.

> **Your project has to be a C++ project, with the plugin installed from source.** A Blueprint-only project packages against the engine's own `UnrealGame.exe` and cannot have a plugin added to it, so the plugin is silently left out - the package still reports success. If your project has no `Source/` folder, add a C++ class from the editor (**Tools > New C++ Class**) before packaging.

## 1. Enable the plugins

Confirm both are enabled in **Edit > Plugins**: search "Live Link", then search "MOVIN".

## 2. Call the Blueprint node at startup

### Option A: Level Blueprint (simplest)

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

### Option B: GameInstance Blueprint (recommended)

Use this if you want the source to persist across level changes:

1. Content Browser > right-click > **Blueprint Class** > search **GameInstance** > create it (e.g. `BP_MyGameInstance`)
2. Open it > in the **Event Graph**, add an **Event Init** node
3. Drag from it > add **Add MOVIN LiveLink Source** (port `11236`)
4. **Compile** and **Save**
5. **Edit > Project Settings > Maps & Modes > Game Instance Class** > set it to your `BP_MyGameInstance`

This runs once at game startup and survives level transitions.

## 3. Package

Package your game normally, and make sure MOVIN Studio is streaming to the configured port when the game runs.

