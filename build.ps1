param(
    [Parameter(Mandatory=$true)][string]$Sdk,
    [Parameter(Mandatory=$true)][string]$Compiler,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build')
)
$ErrorActionPreference = 'Stop'
$sdkRoot = (Resolve-Path -LiteralPath $Sdk).Path
$compilerPath = (Resolve-Path -LiteralPath $Compiler).Path
$null = New-Item -ItemType Directory -Force -Path $BuildDirectory
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$bundle = Join-Path $PSScriptRoot 'Nabd Gate.vst3'
$binaryDirectory = Join-Path $bundle 'Contents/x86_64-win'
$null = New-Item -ItemType Directory -Force -Path $binaryDirectory
$sdkFiles = @(
    'pluginterfaces/base/coreiids.cpp','pluginterfaces/base/funknown.cpp','pluginterfaces/base/ustring.cpp',
    'base/source/baseiids.cpp','base/source/fbuffer.cpp','base/source/fdebug.cpp',
    'base/source/fobject.cpp','base/source/fstreamer.cpp','base/source/fstring.cpp','base/source/updatehandler.cpp',
    'base/thread/source/flock.cpp',
    'public.sdk/source/common/commoniids.cpp','public.sdk/source/common/pluginview.cpp',
    'public.sdk/source/main/pluginfactory.cpp','public.sdk/source/main/dllmain.cpp',
    'public.sdk/source/vst/vstinitiids.cpp','public.sdk/source/vst/vstbus.cpp',
    'public.sdk/source/vst/vstcomponentbase.cpp','public.sdk/source/vst/vstcomponent.cpp',
    'public.sdk/source/vst/vstaudioeffect.cpp','public.sdk/source/vst/vsteditcontroller.cpp',
    'public.sdk/source/vst/vstparameters.cpp'
)
$sources = @((Join-Path $PSScriptRoot 'source/NabdGate.cpp'), (Join-Path $PSScriptRoot 'source/Editor.cpp'))
$sources += $sdkFiles | ForEach-Object { Join-Path $sdkRoot $_ }
foreach ($source in $sources) { if (!(Test-Path -LiteralPath $source)) { throw "Missing source: $source" } }
$flags = @('-std=c++17','-O2','-DNDEBUG','-DRELEASE=1','-DNOMINMAX','-DUNICODE','-D_UNICODE',
    '-D_M_AMD64=100','-D_WIN32_WINNT=0x0601',"-I$sdkRoot",'-Wno-multichar','-Wno-pragma-pack')
$objects = @()
$index = 0
foreach ($source in $sources) {
    $object = Join-Path $buildRoot (([string]$index) + '.o')
    Write-Host ('Compiling ' + [System.IO.Path]::GetFileName($source))
    & $compilerPath @flags '-c' $source '-o' $object
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
    $objects += $object
    $index++
}
$output = Join-Path $binaryDirectory 'Nabd Gate.vst3'
& $compilerPath '-shared' '-static' '-o' $output @objects (Join-Path $PSScriptRoot 'source/exports.def') '-luser32' '-lgdi32' '-lole32' '-luuid'
if ($LASTEXITCODE -ne 0) { throw 'Link failed' }
Write-Host "Built: $output"
