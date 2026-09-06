[CmdletBinding()]
param(
    [string]$BuildDirectory,
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $RepoRoot "build-windows"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $RepoRoot "dist"
}

$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$PackageName = "TapeSister-Windows-x64"
$StagingDirectory = Join-Path $OutputDirectory $PackageName
$ArchivePath = Join-Path $OutputDirectory "$PackageName.zip"

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
if (Test-Path -LiteralPath $StagingDirectory) {
    Remove-Item -LiteralPath $StagingDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $StagingDirectory | Out-Null

& (Join-Path $PSScriptRoot "stage-windows-portable.ps1") `
    -BuildDirectory $BuildDirectory -Destination $StagingDirectory

if (Test-Path -LiteralPath $ArchivePath) {
    Remove-Item -LiteralPath $ArchivePath -Force
}
Compress-Archive -LiteralPath $StagingDirectory -DestinationPath $ArchivePath `
    -CompressionLevel Optimal

Write-Host "Created '$ArchivePath'."
