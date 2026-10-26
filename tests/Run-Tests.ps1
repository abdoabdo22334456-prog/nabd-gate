param(
    [Parameter(Mandatory=$true)][string]$Sdk,
    [Parameter(Mandatory=$true)][string]$Compiler,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../build')
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$sdkRoot=(Resolve-Path -LiteralPath $Sdk).Path
$compilerPath=(Resolve-Path -LiteralPath $Compiler).Path
if (!(Test-Path -LiteralPath (Join-Path $BuildDirectory '17.o'))) {
    & (Join-Path $root 'build.ps1') -Sdk $sdkRoot -Compiler $compilerPath -BuildDirectory $BuildDirectory
}
$buildRoot=(Resolve-Path -LiteralPath $BuildDirectory).Path
$objects=@()
foreach ($index in (2..14 + @(17))) { $objects += Join-Path $buildRoot "$index.o" }
$executable=Join-Path $buildRoot 'HostTests.exe'
& $compilerPath '-std=c++17' '-O2' '-DRELEASE=1' '-D_M_AMD64=100' '-Wno-pragma-pack' "-I$sdkRoot" `
    (Join-Path $PSScriptRoot 'HostTests.cpp') `
    (Join-Path $sdkRoot 'public.sdk/source/common/memorystream.cpp') `
    (Join-Path $sdkRoot 'public.sdk/source/vst/hosting/parameterchanges.cpp') `
    @objects '-static' '-luser32' '-lgdi32' '-lole32' '-luuid' '-o' $executable
if ($LASTEXITCODE -ne 0) { throw 'Host test build failed' }
& $executable (Join-Path $root 'Nabd Gate.vst3/Contents/x86_64-win/Nabd Gate.vst3') (Join-Path $buildRoot 'preview.bmp')
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
