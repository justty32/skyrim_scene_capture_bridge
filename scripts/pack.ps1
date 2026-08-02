#Requires -Version 5.1
<#
.SYNOPSIS
    Package the built SKSE plugin .dll into a MO2-installable .zip.
.DESCRIPTION
    Reads plugin name/version from build/<config>/CMakeCache.txt, stages the
    standard Data/SKSE/Plugins/ layout under pack/, and zips it into dist/.
.EXAMPLE
    scripts/pack.ps1
    scripts/pack.ps1 -Config debug-msvc
#>
[CmdletBinding()]
param(
    [ValidateSet('release-msvc', 'debug-msvc')]
    [string]$Config = 'release-msvc',
    [string]$OutputDir = 'dist'
)

$ErrorActionPreference = 'Stop'

# Must match CONFIG_FOLDER in CMakeLists.txt — rename both together.
$ConfigFolderName = 'SceneCaptureBridge'

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$buildDir  = Join-Path $repoRoot "build\$Config"
$cacheFile = Join-Path $buildDir  'CMakeCache.txt'

if (-not (Test-Path $cacheFile)) {
    throw "No CMakeCache.txt at '$cacheFile'. Run 'cmake --preset build-$Config' first."
}

function Get-CacheValue([string]$Key) {
    $pattern = '^{0}:\w+=(.+)$' -f [regex]::Escape($Key)
    $match   = Select-String -Path $cacheFile -Pattern $pattern | Select-Object -First 1
    if (-not $match) { throw "Cache key '$Key' not found in '$cacheFile'." }
    return $match.Matches[0].Groups[1].Value.Trim()
}

$pluginName    = Get-CacheValue 'CMAKE_PROJECT_NAME'
$pluginVersion = Get-CacheValue 'CMAKE_PROJECT_VERSION'

$dllPath = Join-Path $buildDir "$pluginName.dll"
if (-not (Test-Path $dllPath)) {
    throw "DLL not found at '$dllPath'. Build the project first (cmake --build $buildDir)."
}

$packDir    = Join-Path $repoRoot 'pack'
$pluginsDir = Join-Path $packDir  'Data\SKSE\Plugins'

if (Test-Path $packDir) { Remove-Item -Recurse -Force $packDir }
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null

Copy-Item -Path $dllPath -Destination $pluginsDir

$configSrc = Join-Path $repoRoot 'config'
if (Test-Path $configSrc) {
    $configDst = Join-Path $pluginsDir $ConfigFolderName
    Copy-Item -Recurse -Path $configSrc -Destination $configDst
    Write-Host "Included config/ -> Data\SKSE\Plugins\$ConfigFolderName\"
}

$outDirAbs = if ([System.IO.Path]::IsPathRooted($OutputDir)) { $OutputDir } else { Join-Path $repoRoot $OutputDir }
New-Item -ItemType Directory -Path $outDirAbs -Force | Out-Null

$buildTag = if ($Config -eq 'debug-msvc') { '-Debug' } else { '' }
$zipName  = "$pluginName-$pluginVersion$buildTag.zip"
$zipPath  = Join-Path $outDirAbs $zipName
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }

Compress-Archive -Path (Join-Path $packDir '*') -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host ""
Write-Host "Packaged: $zipPath"
Write-Host "  Drag this .zip into MO2's 'Install a new mod from an archive' dialog."
