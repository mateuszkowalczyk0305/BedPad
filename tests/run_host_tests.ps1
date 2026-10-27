param(
    [string]$IdfPath = $env:IDF_PATH,
    [string]$Compiler = 'gcc'
)
$ErrorActionPreference = 'Stop'
if (-not $IdfPath) { throw 'Set IDF_PATH or pass -IdfPath.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $projectRoot 'build/host-tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$binary = Join-Path $outputDir 'host_tests.exe'
$flags = @('-std=gnu17', '-Wall', '-Wextra', '-Werror', "-I$projectRoot/main",
    "-I$PSScriptRoot/host_stubs", "-I$IdfPath/components/esp_hid/include")
$parser = Join-Path $outputDir 'parser.o'
# IDF suppresses format warnings in this vendor component as well. The host
# build excludes Bluetooth-only branches, leaving a few unused parameters.
& $Compiler @flags '-Wno-format' '-Wno-unused-parameter' '-include' 'stdlib.h' `
    '-c' "$IdfPath/components/esp_hid/src/esp_hid_common.c" '-o' $parser
if ($LASTEXITCODE -ne 0) { throw 'IDF parser host compilation failed.' }
& $Compiler @flags "$projectRoot/main/input_math.c" "$PSScriptRoot/host_tests.c" `
    $parser '-o' $binary
if ($LASTEXITCODE -ne 0) { throw 'Application host test compilation failed.' }
& $binary
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed.' }
