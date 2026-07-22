param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

$clangFormat = Get-Command clang-format -ErrorAction SilentlyContinue
if (-not $clangFormat) {
    $bundled = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022' `
        -Recurse -Filter clang-format.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\Llvm\\bin\\clang-format\.exe$' } |
        Select-Object -First 1
    if ($bundled) { $clangFormat = $bundled }
}
if (-not $clangFormat) {
    throw 'clang-format was not found. Install LLVM or the Visual Studio C++ Clang tools.'
}

$files = Get-ChildItem $repo\src, $repo\tests, $repo\tools -Recurse -File |
    Where-Object { $_.Extension -in '.cpp', '.hpp', '.h' }

if ($Check) {
    & $clangFormat.FullName --dry-run --Werror @($files.FullName)
} else {
    & $clangFormat.FullName -i @($files.FullName)
}
if ($LASTEXITCODE -ne 0) { throw 'clang-format failed' }

