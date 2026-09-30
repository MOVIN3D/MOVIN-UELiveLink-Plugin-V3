# Development and packaging

Supported target: Win64, UE 5.3-5.8. The runtime module uses Windows CNG for SHA-256; no extra
DLL needs to be installed. Build with the toolchain required by your installed engine.

## Build user packages

From the repository root, in PowerShell:

```powershell
./Scripts/Build-Prebuilt.ps1 -WorkRoot D:/_mue-release
# Or build a selected engine:
./Scripts/Build-Prebuilt.ps1 -Versions 5.8 -WorkRoot D:/_mue-58
```

The script runs BuildPlugin for Editor, Development and Shipping, then writes versioned zips and
SHA-256 sidecars into `Prebuilt/<version>/`. Each zip includes `build-info.json` with the source
fingerprint. Choose a fresh short WorkRoot. Existing build/archive paths are refused; the script
never recursively deletes another build. `-SkipBuild` only reuses a successful build whose source
fingerprint still matches, and requires a new output directory if the archive already exists.
Missing engines or failed builds stop the run instead of producing a partial success report.

A prebuilt zip supports Blueprint-only projects **in the editor**. Packaged runtime use requires
a C++ project that links the module; retaining precompiled game objects alone is not proof that a
Blueprint-only packaged executable includes the plugin. Test a C++ packaged project before release.

## Automation tests

Build the plugin from source in an Unreal host project, then run:

```text
UnrealEditor-Cmd.exe HostProject.uproject /Engine/Maps/Entry -AssetGatherAll=false -unattended -NullRHI -nosound
  -ExecCmds="Automation RunTests MOVIN." -TestExit="Automation Test Queue Empty"
  -ReportExportPath=D:/MOVINTests -abslog=D:/MOVINTests.log
```

The tests cover packet boundaries, Unicode, malformed values, frame ordering, port collisions,
source recreation, UDP status, changing skeletons/subjects, LiveLink evaluation and calibration.
An optional `-MOVINStudioPacket=<file>` supplies a packet captured by the Studio sender test for
cross-application verification. Run tests on the oldest and newest supported engine and build all
versions before updating release assets. Studio's adjacent editor tests cover its sender/status UI.

## Internal stream validation

Validation is excluded from user packages and compiled out by default even in editor builds.
For an internal **full source checkout**, set `MOVIN_STREAM_VALIDATION=1` in the build process
environment and rebuild the editor target with `-ForceRulesCompile -NoUBTMakefiles` so cached
build rules cannot retain the old definition. The packaged game target always disables it. The
public packaging script rejects this environment setting.

Validation accepts negative frame indices and internal control messages only in that opt-in build.
It writes only under the local Documents/MOVIN Studio/StreamValidation/UE directory, ignores the
remote directory field, requires a safe session ID and never overwrites an existing session file.
Unset the environment variable and rebuild with the same flags before producing user binaries.
