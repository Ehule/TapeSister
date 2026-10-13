[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$ArchivePath,
    [Parameter(Mandatory = $true)] [string]$StagingDirectory
)

$ErrorActionPreference = "Stop"
$ArchivePath = [System.IO.Path]::GetFullPath($ArchivePath)
$StagingDirectory = [System.IO.Path]::GetFullPath($StagingDirectory)
$PackageName = Split-Path -Leaf $StagingDirectory
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Archive = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
$ExpectedFiles = @{}
try {
    $Entries = @{}
    foreach ($Entry in $Archive.Entries) {
        $Name = $Entry.FullName
        if ($Entries.ContainsKey($Name)) { throw "Duplicate ZIP entry: $Name" }
        if (-not $Name.StartsWith("$PackageName/") -or $Name.Contains('\') -or
            $Name.Split('/') -contains '..') { throw "Invalid ZIP path: $Name" }
        $Entries[$Name] = $true
        $Directory = $Name.EndsWith('/')
        $ExpectedAttributes = if ($Directory) { 0x10 } else { 0x20 }
        if ($Entry.ExternalAttributes -ne $ExpectedAttributes) {
            throw "Unexpected ZIP attributes for $Name : $($Entry.ExternalAttributes)"
        }
        if (-not $Directory) { $ExpectedFiles[$Name] = $Entry.Length }
    }
    foreach ($Name in $Entries.Keys) {
        $Parent = $Name.TrimEnd('/')
        while ($Parent.Contains('/')) {
            $Parent = $Parent.Substring(0, $Parent.LastIndexOf('/'))
            if (-not $Entries.ContainsKey("$Parent/")) {
                throw "ZIP is missing explicit directory $Parent/"
            }
        }
    }
    if (-not $Entries.ContainsKey("$PackageName/cdp/bin/")) {
        throw "ZIP is missing cdp/bin/"
    }
} finally {
    $Archive.Dispose()
}

# Exercise the Windows compressed-folder shell used by Explorer, not only a
# ZIP library. CopyHere is asynchronous, so wait for the complete file manifest.
$ExtractRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("TapeSister-zip-test-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $ExtractRoot | Out-Null
try {
    $Shell = New-Object -ComObject Shell.Application
    $Source = $Shell.NameSpace($ArchivePath)
    $Destination = $Shell.NameSpace($ExtractRoot)
    if ($null -eq $Source -or $null -eq $Destination) { throw "Windows ZIP shell is unavailable" }
    $Destination.CopyHere($Source.Items(), 4 + 16 + 512 + 1024)
    $Deadline = [DateTime]::UtcNow.AddSeconds(120)
    do {
        $Missing = @($ExpectedFiles.Keys | Where-Object {
            $File = Get-Item -LiteralPath (Join-Path $ExtractRoot $_) -ErrorAction SilentlyContinue
            $null -eq $File -or $File.Length -ne $ExpectedFiles[$_]
        })
        if ($Missing.Count -eq 0) { break }
        if ([DateTime]::UtcNow -ge $Deadline) {
            throw "Windows shell extraction did not finish: $($Missing -join ', ')"
        }
        Start-Sleep -Milliseconds 250
    } while ($true)
    $Files = @(Get-ChildItem -LiteralPath $StagingDirectory -File -Recurse -Force)
    if ($Files.Count -ne $ExpectedFiles.Count) { throw "ZIP file count differs from staged runtime" }
    foreach ($File in $Files) {
        $Relative = $File.FullName.Substring($StagingDirectory.Length + 1)
        $Extracted = Join-Path (Join-Path $ExtractRoot $PackageName) $Relative
        if ((Get-FileHash -LiteralPath $File.FullName).Hash -ne
            (Get-FileHash -LiteralPath $Extracted).Hash) { throw "Extracted content differs: $Relative" }
    }
    foreach ($Directory in Get-ChildItem -LiteralPath $ExtractRoot -Directory -Recurse) {
        $Probe = Join-Path $Directory.FullName 'tapesister-write-test.tmp'
        [System.IO.File]::WriteAllText($Probe, 'write test')
        Remove-Item -LiteralPath $Probe
    }
    Write-Host "Windows ZIP metadata, shell extraction, $($Files.Count) file hashes and folder writes passed."
} finally {
    Remove-Item -LiteralPath $ExtractRoot -Recurse -Force
}
