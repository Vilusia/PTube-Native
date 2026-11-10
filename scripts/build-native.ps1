param([switch]$RunTests)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$upstream = Join-Path $projectRoot 'upstream/komi-tube'
$pin = '47bf5677a5254dfc37d8716233e7b1861c51698d'
if (-not (Test-Path -LiteralPath $upstream)) {
    & git -c core.autocrlf=false -c core.eol=lf clone --no-checkout 'https://github.com/kouminiku-9900/komi-tube.git' $upstream
    if ($LASTEXITCODE -ne 0) { throw 'Could not fetch native sources.' }
    & git -C $upstream config core.autocrlf false
    & git -C $upstream config core.eol lf
    & git -C $upstream checkout $pin
    if ($LASTEXITCODE -ne 0) { throw 'Could not select the pinned native sources.' }
}
$actual = (& git -C $upstream rev-parse HEAD).Trim()
if ($actual -ne $pin) { throw 'Existing native checkout differs from sources.lock.json; it was not changed.' }
$linuxRoot = (& wsl.exe -d Ubuntu -- wslpath -a $projectRoot).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Could not locate the workspace in Ubuntu.' }
& wsl.exe -d Ubuntu -- docker build -t ptube-native-builder:1 -f "$linuxRoot/native/Dockerfile" "$linuxRoot/native"
if ($LASTEXITCODE -ne 0) { throw 'Native build environment failed.' }
& wsl.exe -d Ubuntu -- docker run --rm -v "${linuxRoot}:/work" ptube-native-builder:1 sh /work/native/build.sh
if ($LASTEXITCODE -ne 0) { throw 'Native PSP build failed.' }
if ($RunTests) {
    & wsl.exe -d Ubuntu -- docker build -t ptube-native-host:1 -f "$linuxRoot/native/Dockerfile.host" "$linuxRoot/native"
    if ($LASTEXITCODE -ne 0) { throw 'Native host test environment failed.' }
    & wsl.exe -d Ubuntu -- docker run --rm -v "${linuxRoot}:/work" --mount 'type=volume,source=ptube-host-47bf5677-20261109,target=/work/upstream/komi-tube/vendor/tilefinch/build-preset-release' ptube-native-host:1 sh /work/native/test-host.sh
    if ($LASTEXITCODE -ne 0) { throw 'Native host tests failed.' }
}
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $projectRoot 'dist/PTube-native-psp.zip'), (Join-Path $projectRoot 'dist/PTube-native-source.zip')
