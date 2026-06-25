#Requires -Version 5.1
<#
.SYNOPSIS
    Downloads a pinned poppler-windows release and normalizes its layout into
    <OutDir>/bin/, so `make install` can bundle pdftotext.exe alongside mn.exe.

.PARAMETER Version
    Release tag from oschwartz10612/poppler-windows, e.g. "24.08.0-0".

.PARAMETER OutDir
    Destination directory. Will be wiped and recreated.
#>
param(
    [Parameter(Mandatory = $true)] [string] $Version,
    [Parameter(Mandatory = $true)] [string] $OutDir
)

$ErrorActionPreference = 'Stop'

$url = "https://github.com/oschwartz10612/poppler-windows/releases/download/v$Version/Release-$Version.zip"

if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$zip = Join-Path $OutDir 'poppler.zip'

Write-Host "Downloading poppler v$Version..."
Invoke-WebRequest -Uri $url -OutFile $zip

Write-Host "Extracting..."
Expand-Archive -Path $zip -DestinationPath $OutDir -Force
Remove-Item $zip

$exe = Get-ChildItem -Path $OutDir -Filter pdftotext.exe -Recurse | Select-Object -First 1
if ($null -eq $exe) { throw "pdftotext.exe not found in archive" }

$srcBin  = $exe.Directory.FullName
$destBin = Join-Path (Resolve-Path $OutDir) 'bin'

if ($srcBin -ne $destBin) {
    if (Test-Path $destBin) { Remove-Item -Recurse -Force $destBin }
    Move-Item -Path $srcBin -Destination $destBin
}

Write-Host "Bundled poppler ready at $destBin"
Write-Host "Run: make install"
