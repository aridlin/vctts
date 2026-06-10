param(
    [string]$Configuration = "Release",
    [string]$Generator = "Visual Studio 17 2022"
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo "build-release"
$distRoot = Join-Path $repo "dist"
$stage = Join-Path $distRoot "vctts"
$zip = Join-Path $distRoot "vctts-$Configuration.zip"

cmake -S $repo -B $buildDir -G $Generator -DBUILD_TESTS=ON
cmake --build $buildDir --config $Configuration
ctest --test-dir $buildDir -C $Configuration --output-on-failure

if (Test-Path $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item -LiteralPath (Join-Path $buildDir "$Configuration\vctts.exe") -Destination $stage -Force
Copy-Item -LiteralPath (Join-Path $repo "README.md") -Destination $stage -Force

$driverSrc = Join-Path $repo "drivers"
if (Test-Path $driverSrc) {
    Copy-Item -LiteralPath $driverSrc -Destination (Join-Path $stage "drivers") -Recurse -Force
}

if (Test-Path $zip) {
    Remove-Item -LiteralPath $zip -Force
}
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -Force

Write-Host "Release staged at $stage"
Write-Host "Release zip created at $zip"
