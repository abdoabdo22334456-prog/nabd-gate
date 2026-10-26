param(
    [Parameter(Mandatory=$true)][string]$Compiler,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference='Stop'
$compilerPath=(Resolve-Path -LiteralPath $Compiler).Path
$windres=Join-Path (Split-Path -Parent $compilerPath) 'x86_64-w64-mingw32-windres.exe'
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$bundle=(Resolve-Path -LiteralPath (Join-Path $root 'Nabd Gate.vst3')).Path
$null=New-Item -ItemType Directory -Force -Path $BuildDirectory
$buildRoot=(Resolve-Path -LiteralPath $BuildDirectory).Path
$files=Get-ChildItem -LiteralPath $bundle -Recurse -File | Sort-Object FullName
$header=@('#pragma once','struct Payload { int id; const wchar_t* path; const char* hash; };','static const Payload payload[] = {')
$manifest=(Join-Path $PSScriptRoot 'Setup.manifest').Replace('\','/')
$resource=@('#include <windows.h>',"1 RT_MANIFEST `"$manifest`"",'1 VERSIONINFO','FILEVERSION 1,2,0,0','PRODUCTVERSION 1,2,0,0','FILEOS 0x40004','FILETYPE 1','BEGIN','BLOCK "StringFileInfo"','BEGIN','BLOCK "040904b0"','BEGIN','VALUE "CompanyName", "Nabd Audio\0"','VALUE "FileDescription", "Nabd Gate VST3 Installer\0"','VALUE "FileVersion", "1.2.0\0"','VALUE "ProductName", "Nabd Gate\0"','END','END','BLOCK "VarFileInfo"','BEGIN','VALUE "Translation", 0x409, 1200','END','END')
$id=100
foreach($file in $files) {
    $relative=[IO.Path]::GetRelativePath($bundle,$file.FullName).Replace('\','/')
    $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $header+="{$id, L`"$relative`", `"$hash`"},"
    $resource+="$id RCDATA `"$($file.FullName.Replace('\','/'))`""
    $id++
}
$header+='};'
Set-Content -LiteralPath (Join-Path $buildRoot 'Payload.h') -Value $header -Encoding utf8
Set-Content -LiteralPath (Join-Path $buildRoot 'Setup.rc') -Value $resource -Encoding utf8
$object=Join-Path $buildRoot 'Setup-resource.o'
& $windres '-i' (Join-Path $buildRoot 'Setup.rc') '-o' $object
if($LASTEXITCODE -ne 0){throw 'Installer resources failed'}
& $compilerPath '-std=c++17' '-O2' '-static' '-municode' '-mwindows' '-DUNICODE' '-D_UNICODE' '-D_WIN32_WINNT=0x0A00' "-I$buildRoot" (Join-Path $PSScriptRoot 'Setup.cpp') $object '-lshell32' '-lole32' '-ladvapi32' '-lbcrypt' '-luuid' '-o' $Output
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
Write-Output "Built installer: $Output"
