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
# Write every directory explicitly, including cdp/bin. Compress-Archive emitted
# only some parent folders, with no DOS directory bit. Keep the portable ZIP's
# metadata independent of build-machine attributes and access-control settings.
# Windows PowerShell 5.1 needs both assemblies loaded before resolving the ZIP
# enums; loading FileSystem alone does not make ZipArchiveMode available.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Archive = [System.IO.Compression.ZipFile]::Open(
    $ArchivePath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $Items = @((Get-Item -LiteralPath $StagingDirectory)) + @(
        Get-ChildItem -LiteralPath $StagingDirectory -Recurse -Force |
            Sort-Object FullName
    )
    foreach ($Item in $Items) {
        if ($Item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            throw "Portable package must contain ordinary files and folders: $($Item.FullName)"
        }
        $RelativePath = $Item.FullName.Substring($OutputDirectory.TrimEnd('\', '/').Length + 1).Replace('\', '/')
        if ($Item.PSIsContainer) {
            $Entry = $Archive.CreateEntry("$RelativePath/")
            $Entry.ExternalAttributes = 0x10 # DOS directory; no read-only/system flags
        } else {
            $Entry = [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $Archive, $Item.FullName, $RelativePath,
                [System.IO.Compression.CompressionLevel]::Optimal)
            $Entry.ExternalAttributes = 0x20 # Ordinary archive file
        }
    }
} finally {
    $Archive.Dispose()
}

Write-Host "Created '$ArchivePath'."
