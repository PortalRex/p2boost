param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

$clangFormatCommand = Get-Command clang-format -ErrorAction SilentlyContinue
$clangFormatPath = if ($clangFormatCommand) { $clangFormatCommand.Source } else { $null }
if (-not $clangFormatPath) {
    $bundled = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022' `
        -Recurse -Filter clang-format.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\Llvm\\bin\\clang-format\.exe$' } |
        Select-Object -First 1
    if ($bundled) { $clangFormatPath = $bundled.FullName }
}
if (-not $clangFormatPath) {
    throw 'clang-format was not found. Install LLVM or the Visual Studio C++ Clang tools.'
}

$files = Get-ChildItem $repo\src, $repo\tests, $repo\tools -Recurse -File |
    Where-Object { $_.Extension -in '.cpp', '.hpp', '.h' }

if ($Check) {
    & $clangFormatPath --dry-run --Werror @($files.FullName)
} else {
    & $clangFormatPath -i @($files.FullName)
}
if ($LASTEXITCODE -ne 0) { throw 'clang-format failed' }
