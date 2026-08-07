param(
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [string]$Generator = '',
    [string]$BuildDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

if (-not $Generator) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio locator not found.' }
    $version = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationVersion | Select-Object -First 1
    if ($version -match '^17\.') { $Generator = 'Visual Studio 17 2022' }
    elseif ($version -match '^16\.') { $Generator = 'Visual Studio 16 2019' }
    else { throw "No supported x86 Visual C++ toolchain found (version '$version')." }
}

if (-not $BuildDirectory) {
    $slug = if ($Generator -eq 'Visual Studio 17 2022') { 'vs2022-x86' } else { 'vs2019-x86' }
    $BuildDirectory = Join-Path $repo "build\$slug"
}

& cmake -S $repo -B $BuildDirectory -G $Generator -A Win32
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& cmake --build $BuildDirectory --config $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
