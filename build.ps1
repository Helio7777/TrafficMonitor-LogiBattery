param(
    [ValidateSet('x64','Win32')]
    [string]$Arch = 'x64',
    [ValidateSet('Release','Debug')]
    [string]$Config = 'Release',
    [string]$ProjectUrl = ''
)

$ErrorActionPreference = 'Stop'

# MSBuild treats environment keys case-insensitively and fails when a parent
# process supplies both Path and PATH. Normalize an uppercase entry for child
# processes before invoking CMake.
$uppercasePath = Get-ChildItem Env: | Where-Object { $_.Name -ceq 'PATH' } | Select-Object -First 1
if ($uppercasePath) {
    $env:Path = $uppercasePath.Value
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
}

$cmakeCommand = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmakeCommand) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
        if ($installPath) {
            $bundledCmake = Join-Path $installPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $bundledCmake) {
                $cmakeCommand = $bundledCmake
            }
        }
    }
}
if (-not $cmakeCommand) {
    throw 'CMake was not found. Install the Visual Studio C++ CMake tools or add cmake.exe to PATH.'
}

$buildDir = "build-$Arch"
$configureArgs = @(
    '-S', '.', '-B', $buildDir,
    '-G', 'Visual Studio 17 2022', '-A', $Arch,
    "-DLOGIBATTERY_PROJECT_URL=$ProjectUrl"
)
& $cmakeCommand @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE."
}
& $cmakeCommand --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}

$dll = Resolve-Path "$buildDir/$Config/LogiBatteryPlugin.dll"
Write-Host "Built: $dll"
