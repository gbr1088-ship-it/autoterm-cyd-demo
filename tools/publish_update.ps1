# Publishes a new firmware version for the screens to download over WiFi.
#
#   1. raise FW_VERSION in platformio.ini
#   2. powershell -ExecutionPolicy Bypass -File tools\publish_update.ps1
#
# Builds every version (avl, kolex, beneteau), copies them into a local clone of the
# secret Gist named in UPDATE_BASE_URL, writes version.txt with each file's
# SHA-256 and size, and pushes. Also refreshes the USB files in firmware\.
# GitHub's download servers can take up to 5 minutes to show the new files.

param([string]$GistDir = (Join-Path $PSScriptRoot '..\..\autoterm_updates_gist'))
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$envs = 'avl', 'kolex', 'beneteau'

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    $desktopGit = Get-ChildItem "$env:LOCALAPPDATA\GitHubDesktop\app-*\resources\app\git\cmd" -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($desktopGit) { $env:PATH = "$($desktopGit.FullName);$env:PATH" } else { throw 'git not found' }
}
$py = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"

$ini = Get-Content (Join-Path $root 'platformio.ini') -Raw
$version = [int]([regex]::Match($ini, 'FW_VERSION=(\d+)').Groups[1].Value)
$baseUrl = [regex]::Match($ini, 'UPDATE_BASE_URL=\\"([^"\\]*)\\"').Groups[1].Value
$gistId = [regex]::Match($baseUrl, 'gist\.githubusercontent\.com/[^/]+/([0-9a-f]+)/raw/').Groups[1].Value
if (-not $gistId) { throw "UPDATE_BASE_URL in platformio.ini isn't a Gist raw address: '$baseUrl'" }

if (-not (Test-Path $GistDir)) { git clone "https://gist.github.com/$gistId.git" $GistDir }
git -C $GistDir config core.autocrlf false  # never touch line endings: the .bin files must stay byte-exact
git -C $GistDir pull --quiet
$published = 0
$versionFile = Join-Path $GistDir 'version.txt'
if (Test-Path $versionFile) {
    $m = [regex]::Match((Get-Content $versionFile -Raw), 'version=(\d+)')
    if ($m.Success) { $published = [int]$m.Groups[1].Value }
}
if ($version -le $published) { throw "FW_VERSION is $version but version $published is already published: raise it first" }

Push-Location $root
try {
    & $py -m platformio run $($envs | ForEach-Object { '-e', $_ })
    if ($LASTEXITCODE) { throw 'build failed' }
} finally { Pop-Location }

$lines = @("version=$version")
$pio = "$env:USERPROFILE\.platformio\packages"
foreach ($e in $envs) {
    $build = Join-Path $root ".pio\build\$e"
    $bin = Join-Path $GistDir "autoterm_$e.bin"
    Copy-Item (Join-Path $build 'firmware.bin') $bin -Force
    $sha = (Get-FileHash $bin -Algorithm SHA256).Hash.ToLower()
    $lines += "$e=$sha $((Get-Item $bin).Length)"
    # the single-file image for flashing over USB at 0x0
    & $py "$pio\tool-esptoolpy\esptool.py" --chip esp32 merge_bin -o (Join-Path $root "firmware\autoterm_$e.bin") `
        --flash_mode dio --flash_freq keep --flash_size 4MB 0x1000 "$build\bootloader.bin" 0x8000 "$build\partitions.bin" `
        0xe000 "$pio\framework-arduinoespressif32\tools\partitions\boot_app0.bin" 0x10000 "$build\firmware.bin" | Out-Null
}
[IO.File]::WriteAllText($versionFile, ($lines -join "`n") + "`n", (New-Object Text.UTF8Encoding $false))

git -C $GistDir add -A
git -C $GistDir commit --quiet -m "version $version"
git -C $GistDir push --quiet
Write-Output "published version ${version}:"
$lines | ForEach-Object { "  $_" }
