param([switch]$Force)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$downloads = Join-Path $repo '.tools\downloads'
New-Item -ItemType Directory -Force $downloads | Out-Null

function Download([string]$Url, [string]$Path) {
    if ($Force -or -not (Test-Path -LiteralPath $Path)) {
        & curl.exe --ssl-no-revoke -L --fail --retry 3 -o $Path $Url
        if ($LASTEXITCODE -ne 0) { throw "download failed: $Url" }
    }
}

$jdkZip = Join-Path $downloads 'jdk21.zip'
Download 'https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.11%2B10/OpenJDK21U-jdk_x64_windows_hotspot_21.0.11_10.zip' $jdkZip
$jdkHash = (Get-FileHash $jdkZip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($jdkHash -ne 'd3625e7cadf23787ea540229544b6e2ab494b3b54da1801879e583e1dfee0a64') { throw "JDK checksum mismatch: $jdkHash" }
if (-not (Test-Path "$repo\.tools\jdk21")) { Expand-Archive $jdkZip "$repo\.tools\jdk21" }

$ghidraZip = Join-Path $downloads 'ghidra.zip'
Download 'https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.2_build/ghidra_12.1.2_PUBLIC_20260605.zip' $ghidraZip
if (-not (Test-Path "$repo\.tools\ghidra")) { Expand-Archive $ghidraZip "$repo\.tools\ghidra" }

Download 'https://github.com/GameTechDev/PresentMon/releases/download/v2.5.1/PresentMon-2.5.1-x64.exe' "$downloads\PresentMon.exe"

