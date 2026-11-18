# Builds Sun Loops Studio for Windows (0.75.0): the installer Sun-Loops-Studio-<version>-Windows-x64-Setup.exe and the zip
# Sun-Loops-Studio-<version>-Windows-x64.zip (the same files, to unpack anywhere and start without installing), in dist\.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1
#
# Needs: Windows 10 or 11 (x64) with Visual Studio 2022 (or newer) or its Build Tools, workloads "Desktop development with C++" and the
# component "C++ Clang Compiler for Windows" (CMake and Ninja come with them), Git, and Inno Setup 6 for the installer
# (winget install JRSoftware.InnoSetup; without it only the zip is made). The script finds Visual Studio by itself; it can also be
# run from a "x64 Native Tools Command Prompt for VS 2022".
#
# -BuildDir and -DistDir choose the folders (default build-windows\ and dist\). The first build downloads JUCE, spdlog, json,
# curl and llama.cpp and takes a while; later builds only redo what changed.
param(
    [string]$BuildDir = "",
    [string]$DistDir = "",
    [switch]$NoInstaller
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..").Path
if (-not $BuildDir) { $BuildDir = Join-Path $root "build-windows" }
if (-not $DistDir) { $DistDir = Join-Path $root "dist" }
# Whole paths (Inno Setup reads relative ones from the .iss file's folder).
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path (Get-Location).Path $BuildDir }
if (-not [System.IO.Path]::IsPathRooted($DistDir)) { $DistDir = Join-Path (Get-Location).Path $DistDir }

$version = (Select-String -Path "$root\CMakeLists.txt" -Pattern '^project\(SunLoopsStudio VERSION ([0-9.]+)').Matches[0].Groups[1].Value
if (-not $version) { throw "cannot read the version from CMakeLists.txt" }
Write-Host "Sun Loops Studio $version for Windows"

# --- the compiler's environment (vcvars64), unless this is already a Visual Studio prompt ---
if (-not (Get-Command clang-cl -ErrorAction SilentlyContinue) -or -not $env:VCINSTALLDIR) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "Visual Studio 2022 (or its Build Tools) was not found" }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "Visual Studio has no C++ tools: add the workload 'Desktop development with C++'" }
    $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
    Write-Host "Using $vcvars"
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}
if (-not (Get-Command clang-cl -ErrorAction SilentlyContinue)) {
    throw "clang-cl was not found: add the Visual Studio component 'C++ Clang Compiler for Windows'"
}

# --- configure and build ---
$cmakeArgs = @("-S", $root, "-B", $BuildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
               "-DCMAKE_C_COMPILER=clang-cl", "-DCMAKE_CXX_COMPILER=clang-cl", "-DSLS_BUILD_TEST_PLUGINS=OFF")
if ($env:FETCHCONTENT_SOURCE_DIR_JUCE) { $cmakeArgs += "-DFETCHCONTENT_SOURCE_DIR_JUCE=$env:FETCHCONTENT_SOURCE_DIR_JUCE" }
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
& cmake --build $BuildDir --target SunLoopsStudio
if ($LASTEXITCODE -ne 0) { throw "the build failed" }

# --- the program's folder, as installed: the program, its symbols, Helios's helper, the bundled plugins in VST3\ ---
$built = Join-Path $BuildDir "SunLoopsStudio_artefacts\Release"
$exe = Join-Path $built "Sun Loops Studio.exe"
if (-not (Test-Path $exe)) { throw "the program was not built: $exe" }
$stageRoot = Join-Path $BuildDir "stage"
$stage = Join-Path $stageRoot "Sun Loops Studio"
if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }
New-Item -ItemType Directory -Force -Path (Join-Path $stage "VST3") | Out-Null
Copy-Item $exe $stage
$pdb = Join-Path $built "Sun Loops Studio.pdb" # the function names of crash reports
if (Test-Path $pdb) { Copy-Item $pdb $stage } else { Write-Warning "no Sun Loops Studio.pdb: crash reports will have no function names" }
$helper = Join-Path $built "sun-loops-helios.exe"
if (Test-Path $helper) {
    Copy-Item $helper $stage
    Copy-Item "$root\third_party\llama.cpp\LICENSE-llama.cpp.txt" $stage
} else {
    Write-Warning "sun-loops-helios.exe was not built (Helios's LLM mode will say so)"
}
foreach ($plugin in "SunBurst", "HexForge", "SunSymphony", "SunRiff") {
    $bundle = Join-Path $BuildDir "${plugin}_artefacts\Release\VST3\$plugin.vst3"
    if (Test-Path $bundle) { Copy-Item -Recurse $bundle (Join-Path $stage "VST3") }
    else { Write-Warning "$plugin.vst3 was not built" }
}
Copy-Item "$root\LICENSE" (Join-Path $stage "LICENSE.txt")
# 0.76.0: the ASIO SDK's licence (Steinberg's, used under GPLv3), next to the program that includes it
$asioLicense = Get-ChildItem (Join-Path $BuildDir "_deps\asiosdk-src") -Filter "LICENSE*.txt" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($asioLicense) { Copy-Item $asioLicense.FullName (Join-Path $stage "LICENSE-ASIO-SDK.txt") }
else { Write-Warning "no ASIO SDK licence found (built without ASIO?)" }
# 0.77.1: libcurl is linked into the program on Windows; its licence asks for its notice to go with every copy
$curlLicense = Join-Path $BuildDir "_deps\curl-src\COPYING"
if (Test-Path $curlLicense) { Copy-Item $curlLicense (Join-Path $stage "LICENSE-curl.txt") }
else { Write-Warning "no curl licence found" }

# --- the zip ---
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
$zip = Join-Path $DistDir "Sun-Loops-Studio-$version-Windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Built $zip ($([math]::Round((Get-Item $zip).Length / 1MB)) MB)"

# --- the installer (Inno Setup 6) ---
if ($NoInstaller) { return }
$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    foreach ($candidate in "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
                           "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") {
        if (Test-Path $candidate) { $iscc = $candidate; break }
    }
}
if (-not $iscc) {
    Write-Warning "Inno Setup 6 was not found (winget install JRSoftware.InnoSetup): only the zip was made"
    return
}
$icon = Join-Path $BuildDir "SunLoopsStudio_artefacts\JuceLibraryCode\icon.ico" # the program's icon, made by JUCE from the PNG
$isccArgs = @("/Qp", "/DAppVersion=$version", "/DSourceDir=$stage", "/DOutputDir=$DistDir")
if (Test-Path $icon) { $isccArgs += "/DSetupIcon=$icon" }
& $iscc @isccArgs "$root\scripts\windows\SunLoopsStudio.iss"
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
$setup = Join-Path $DistDir "Sun-Loops-Studio-$version-Windows-x64-Setup.exe"
Write-Host "Built $setup ($([math]::Round((Get-Item $setup).Length / 1MB)) MB)"
