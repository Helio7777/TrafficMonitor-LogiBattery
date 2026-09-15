param(
    [ValidateSet('x64','Win32')]
    [string]$Arch = 'x64',
    [ValidateSet('Release','Debug')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$buildDir = "build-$Arch"
cmake -S . -B $buildDir -G "Visual Studio 17 2022" -A $Arch
cmake --build $buildDir --config $Config

$dll = Resolve-Path "$buildDir/$Config/LogiBatteryPlugin.dll"
Write-Host "Built: $dll"
