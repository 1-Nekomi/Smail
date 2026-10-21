param(
    [string]$QtRoot = 'D:\Application\QtTools\6.11.2\mingw_64',
    [string]$CompilerBin = 'D:\Application\QtTools\Tools\mingw1310_64\bin',
    [string]$NsisCompiler = '',
    [switch]$SmokeIdentity
)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $NsisCompiler) { $NsisCompiler = Join-Path $workspace 'build\packaging-tools\nsis-3.11\makensis.exe' }
if (-not (Test-Path -LiteralPath $NsisCompiler)) { throw 'Install NSIS and pass -NsisCompiler <makensis.exe>' }
$version = [regex]::Match((Get-Content (Join-Path $workspace 'CMakeLists.txt') -Raw), 'project\(Smail VERSION ([0-9.]+)').Groups[1].Value
$env:PATH = "$CompilerBin;$QtRoot\bin;$env:PATH"
& cmake -S $workspace -B "$workspace\build" -G Ninja "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_CXX_COMPILER=$CompilerBin\g++.exe" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
& cmake --build "$workspace\build" -j 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
$stage = Join-Path $workspace ('build\installer-stage-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath "$workspace\build\Smail.exe" -Destination $stage
& "$QtRoot\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "$stage\Smail.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
Copy-Item -LiteralPath "$PSScriptRoot\INSTALL-README.txt","$PSScriptRoot\THIRD-PARTY-NOTICES.txt" -Destination $stage
New-Item -ItemType Directory -Path "$stage\licenses" | Out-Null
$qtVersion = ((Get-Item "$QtRoot\bin\Qt6Core.dll").VersionInfo.ProductVersion.Split('.') | Select-Object -First 3) -join '.'
foreach ($module in @('qtbase','qtsvg')) { Copy-Item -LiteralPath "$QtRoot\sbom\$module-$qtVersion.spdx.json" -Destination "$stage\licenses" }
$install = [Collections.Generic.List[string]]::new()
$uninstall = [Collections.Generic.List[string]]::new()
foreach ($file in (Get-ChildItem -LiteralPath $stage -Recurse -File)) {
    $relative = $file.FullName.Substring($stage.Length+1)
    if ($relative.Contains('$') -or $relative.Contains('"')) { throw 'Unsupported filename' }
    $parent = Split-Path -Parent $relative
    $install.Add('SetOutPath "$INSTDIR\' + $parent + '"')
    $install.Add('File "' + $file.FullName + '"')
    $uninstall.Add('Delete "$INSTDIR\' + $relative + '"')
}
foreach ($dir in (Get-ChildItem -LiteralPath $stage -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending)) {
    $uninstall.Add('RMDir "$INSTDIR\' + $dir.FullName.Substring($stage.Length+1) + '"')
}
[IO.File]::WriteAllLines("$stage\install.nsh",$install,[Text.UTF8Encoding]::new($true))
[IO.File]::WriteAllLines("$stage\uninstall.nsh",$uninstall,[Text.UTF8Encoding]::new($true))
New-Item -ItemType Directory -Force -Path "$workspace\dist" | Out-Null
$label = if ($SmokeIdentity) { 'Smail-Smoke' } else { 'Smail' }
$output = "$workspace\dist\$label-$version-windows-x64-setup.exe"
$nsisArgs = @('/V3','/INPUTCHARSET','UTF8',"/DAPP_VERSION=$version","/DOUTPUT_FILE=$output","/DINSTALL_MANIFEST=$stage\install.nsh","/DUNINSTALL_MANIFEST=$stage\uninstall.nsh")
if ($SmokeIdentity) { $nsisArgs += @('/DAPP_ID=Smail.InstallerSmoke','/DAPP_NAME=Smail Installer SmokeTest') }
& $NsisCompiler @nsisArgs "$PSScriptRoot\Smail.nsi"
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed' }
$hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$output.sha256", "$hash  $([IO.Path]::GetFileName($output))`n")
Write-Output "Installer: $output"
