$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$registry = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Smail.InstallerSmoke'
$desktop = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Smail Installer SmokeTest.lnk'
$menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Smail Installer SmokeTest'
if ((Test-Path $registry) -or (Test-Path -LiteralPath $desktop) -or (Test-Path -LiteralPath $menu)) { throw 'Old smoke registration exists; inspect before retrying' }
$version = [regex]::Match((Get-Content "$workspace\CMakeLists.txt" -Raw), 'project\(Smail VERSION ([0-9.]+)').Groups[1].Value
$target = [IO.Path]::GetFullPath((Join-Path $workspace ('build\install-smoke-' + [guid]::NewGuid().ToString('N'))))
if (-not $target.StartsWith($workspace + '\build\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe target' }
$p = Start-Process -FilePath "$workspace\dist\Smail-Smoke-$version-windows-x64-setup.exe" -ArgumentList "/S /D=$target" -WindowStyle Hidden -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "Installer exit: $($p.ExitCode)" }
foreach ($path in @("$target\Smail.exe","$target\Uninstall.exe",$desktop,$menu,$registry)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing installed artifact: $path" }
}
if ((Get-FileHash "$workspace\build\Smail.exe").Hash -ne (Get-FileHash "$target\Smail.exe").Hash) { throw 'Installed binary differs' }
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $p = Start-Process -FilePath "$target\Smail.exe" -ArgumentList "--smoke-test --data-dir $target\test-state" -WindowStyle Hidden -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "App smoke exit: $($p.ExitCode)" }
} finally { $env:PATH = $savedPath }
[IO.File]::WriteAllText("$target\user-attachment.txt",'preserve-user-data')
Copy-Item -LiteralPath "$target\Uninstall.exe" -Destination "$workspace\build\smoke-uninstall.exe" -Force
# Target is verified above; the uninstaller deletes only its build-time inventory.
$p = Start-Process -FilePath "$workspace\build\smoke-uninstall.exe" -ArgumentList "/S _?=$target" -WindowStyle Hidden -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "Uninstaller exit: $($p.ExitCode)" }
foreach ($path in @("$target\Smail.exe","$target\Uninstall.exe",$desktop,$menu,$registry)) {
    if (Test-Path -LiteralPath $path) { throw "Uninstaller left artifact: $path" }
}
if ((Get-Content "$target\user-attachment.txt" -Raw) -ne 'preserve-user-data') { throw 'User file removed' }
if (-not (Test-Path "$target\test-state\history.sqlite")) { throw 'User state removed' }
Write-Output 'PASS: install, shortcuts, registration, binary hash, isolated UI/TLS startup, uninstall, user-data preservation.'
