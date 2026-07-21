$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    & cmake --preset vs2022-x86
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    & cmake --build --preset release -- /m
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    & ctest --preset release
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
} finally { Pop-Location }

