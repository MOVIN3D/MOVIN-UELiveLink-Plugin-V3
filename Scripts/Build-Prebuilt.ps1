<#
.SYNOPSIS
Build versioned Win64 editor packages; retain game objects for C++ projects.
.DESCRIPTION
Requires installed UE versions. Refuses existing build/staging/archive paths.
-SkipBuild only accepts a successful build with the same source fingerprint.
Blueprint-only prebuilt installations are supported in the editor. Packaged
runtime use still requires a C++ project that links this runtime module.
Internal validation and automation test sources are excluded from user zips.
#>
[CmdletBinding()]
param(
    [string[]] $Versions = @('5.3', '5.4', '5.5', '5.6', '5.7', '5.8'),
    [string] $EngineRoot = 'C:\Program Files\Epic Games',
    [string] $WorkRoot = "D:\_mue-$([DateTime]::Now.ToString('yyyyMMdd-HHmmss'))",
    [string] $OutDir,
    [switch] $KeepSymbols,
    [switch] $SkipBuild
)
$ErrorActionPreference = 'Stop'
$PluginRoot = Split-Path -Parent $PSScriptRoot
$UPlugin = Join-Path $PluginRoot 'MOVINLiveLink.uplugin'
$PluginVersion = (Get-Content -LiteralPath $UPlugin -Raw | ConvertFrom-Json).VersionName
if (-not $OutDir) { $OutDir = Join-Path $PluginRoot "Prebuilt\$PluginVersion" }
$WorkRoot = [IO.Path]::GetFullPath($WorkRoot)
$OutDir = [IO.Path]::GetFullPath($OutDir)
if ($WorkRoot.Length -gt 50) { throw 'Use a short WorkRoot to stay within Unreal build path limits.' }
if ($env:MOVIN_STREAM_VALIDATION -eq '1') { throw 'Unset MOVIN_STREAM_VALIDATION before building a public package.' }
foreach ($v in $Versions) {
    if ($v -notmatch '^5\.[3-8]$') { throw "Unsupported engine version: $v" }
    if (-not (Test-Path -LiteralPath (Join-Path $EngineRoot "UE_$v\Engine\Build\BatchFiles\RunUAT.bat"))) { throw "UE $v is not installed." }
}
$inputs = @($UPlugin) + @(Get-ChildItem (Join-Path $PluginRoot 'Source'),(Join-Path $PluginRoot 'Config'),(Join-Path $PluginRoot 'Resources') -Recurse -File | ForEach-Object FullName)
$fingerprint = ($inputs | Sort-Object | ForEach-Object { $_.Substring($PluginRoot.Length) + ':' + (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }) -join "`n"
$sha = [Security.Cryptography.SHA256]::Create()
try { $sourceHash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($fingerprint))).Replace('-', '').ToLowerInvariant() }
finally { $sha.Dispose() }
New-Item -ItemType Directory -Force -Path $WorkRoot, $OutDir | Out-Null
$results = @()
foreach ($v in $Versions) {
    $tag = $v.Replace('.', '')
    $package = Join-Path $WorkRoot $tag
    $receipt = Join-Path $WorkRoot "$tag-build.json"
    $stageRoot = Join-Path $WorkRoot "stage$tag-$([Guid]::NewGuid().ToString('N').Substring(0,8))"
    $stage = Join-Path $stageRoot 'MOVINLiveLinkPlugin'
    $zip = Join-Path $OutDir "MOVINLiveLink-$PluginVersion-UE$v-Win64.zip"
    if (Test-Path -LiteralPath $zip) { throw "Archive already exists: $zip" }
    if ($SkipBuild) {
        $previous = Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
        if ($previous.sourceHash -ne $sourceHash -or -not (Test-Path -LiteralPath $package)) { throw "UE $v build is missing or stale." }
    }
    else {
        if (Test-Path -LiteralPath $package) { throw "Build directory already exists: $package. Choose a fresh WorkRoot." }
        $uat = Join-Path $EngineRoot "UE_$v\Engine\Build\BatchFiles\RunUAT.bat"
        $log = Join-Path $WorkRoot "build$tag.log"
        Write-Host "Building UE $v (log: $log)"
        & $uat BuildPlugin "-Plugin=$UPlugin" "-Package=$package" -TargetPlatforms=Win64 -NoDeleteHostProject *> $log
        if ($LASTEXITCODE -ne 0) { throw "UE $v build failed; see $log" }
        @{engine=$v; version=$PluginVersion; sourceHash=$sourceHash; builtAt=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $receipt
    }
    New-Item -ItemType Directory -Path $stage | Out-Null
    foreach ($entry in @('MOVINLiveLink.uplugin', 'Binaries', 'Intermediate', 'Source', 'Config', 'Resources')) {
        $path = Join-Path $package $entry
        if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $stage -Recurse }
    }
    foreach ($entry in @('MOVINman_V3_Puppet_UE.fbx', 'README.md', 'CHANGELOG.md', 'docs')) {
        Copy-Item -LiteralPath (Join-Path $PluginRoot $entry) -Destination $stage -Recurse
    }
    $root = (Resolve-Path -LiteralPath $stage).Path + [IO.Path]::DirectorySeparatorChar
    $remove = @(Get-ChildItem (Join-Path $stage 'Source') -Recurse -File | Where-Object { $_.Name -in @('MOVINStreamValidation.cpp','MOVINStreamValidation.h','MOVINValidationProtocol.h') -or $_.FullName -match '\\Tests\\' })
    if (-not $KeepSymbols) { $remove += @(Get-ChildItem -LiteralPath $stage -Recurse -Filter '*.pdb' -File) }
    foreach ($file in $remove) {
        if (-not $file.FullName.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { throw "Outside staging directory: $($file.FullName)" }
        Remove-Item -LiteralPath $file.FullName -Force
    }
    $tests = Join-Path $stage 'Source\MOVINLiveLink\Private\Tests'
    if ((Test-Path -LiteralPath $tests) -and @(Get-ChildItem -LiteralPath $tests -Force).Count -eq 0) {
        Remove-Item -LiteralPath $tests
    }
    if (-not (Test-Path -LiteralPath (Join-Path $stage 'Binaries\Win64\UnrealEditor-MOVINLiveLink.dll'))) { throw 'Editor module is missing.' }
    $markers = @(Get-ChildItem (Join-Path $stage 'Intermediate') -Recurse -Filter '*.precompiled' | Where-Object FullName -Match '\\UnrealGame\\')
    if ($markers.Count -lt 2) { throw 'Development/Shipping game objects are missing.' }
    Copy-Item -LiteralPath $receipt -Destination (Join-Path $stage 'build-info.json')
    Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    $results += [pscustomobject]@{engine=$v; version=$PluginVersion; file=(Split-Path $zip -Leaf); sha256=$hash; sourceHash=$sourceHash; bytes=(Get-Item -LiteralPath $zip).Length}
    "$hash  $(Split-Path $zip -Leaf)" | Set-Content -LiteralPath "$zip.sha256"
    Write-Host "Packaged UE ${v}: $zip"
}
$manifest = Join-Path $OutDir "manifest-$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')).json"
$results | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $manifest
$results | Format-Table engine,file,bytes
