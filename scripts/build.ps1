param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) {
    $linuxRoot = (& wsl.exe -d Ubuntu -- wslpath -a $projectRoot).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Could not locate the workspace in Ubuntu.' }
    & wsl.exe -d Ubuntu -- docker run --rm -v "${linuxRoot}/psp:/src" -w /src 'pspdev/pspdev@sha256:ee385df3aa783189d5a033af5eb1d4a9cdd93718fb8bf652ec7bb48e54038a8b' make -B
    if ($LASTEXITCODE -ne 0) { throw 'PSP build failed.' }
}
$bootPath = Join-Path $projectRoot 'psp/EBOOT.PBP'
if (-not (Test-Path -LiteralPath $bootPath)) { throw 'Build EBOOT.PBP first.' }
$releaseRoot = Join-Path $projectRoot 'dist'
$pspStage = Join-Path $releaseRoot 'psp/PSP/GAME/PTube'
$serverStage = Join-Path $releaseRoot 'vps/server'
New-Item -ItemType Directory -Force -Path $pspStage, $serverStage | Out-Null
Copy-Item -LiteralPath $bootPath -Destination $pspStage
Copy-Item -LiteralPath (Join-Path $projectRoot 'psp/server.txt') -Destination $pspStage
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/UBUNTU-SETUP.md') -Destination (Join-Path $releaseRoot 'psp/SETUP.md')
$serverFiles = @('server.mjs','jobs.mjs','prepare.py','package.json','Dockerfile','compose.yaml','.env.example','.dockerignore','server.test.mjs','integration.mjs')
foreach ($name in $serverFiles) { Copy-Item -LiteralPath (Join-Path $projectRoot "server/$name") -Destination $serverStage }
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/UBUNTU-SETUP.md') -Destination (Join-Path $releaseRoot 'vps/SETUP.md')
Compress-Archive -Path (Join-Path $releaseRoot 'psp/*') -DestinationPath (Join-Path $releaseRoot 'PTube-alpha-psp.zip') -Force
# ZipFile includes dotfiles (.env.example and .dockerignore), unlike some
# Compress-Archive implementations on Windows.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$serverZip = Join-Path $releaseRoot 'PTube-alpha-server.zip'
if (Test-Path -LiteralPath $serverZip) { Remove-Item -LiteralPath $serverZip }
$openedZip = [System.IO.Compression.ZipFile]::Open($serverZip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($name in $serverFiles) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($openedZip, (Join-Path $serverStage $name), "server/$name") | Out-Null
    }
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($openedZip, (Join-Path $releaseRoot 'vps/SETUP.md'), 'SETUP.md') | Out-Null
} finally { $openedZip.Dispose() }
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $releaseRoot 'PTube-alpha-psp.zip'), $serverZip
