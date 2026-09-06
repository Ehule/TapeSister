[CmdletBinding()]
param(
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

if ($env:MSYSTEM -ne "UCRT64") {
    throw @"
TapeSister Windows builds require an MSYS2 UCRT64 terminal.
Open "MSYS2 UCRT64", return to the TapeSister folder, and run:
  powershell.exe -ExecutionPolicy Bypass -File scripts/build-windows-portable.ps1
"@
}

$Bash = Get-Command bash.exe -ErrorAction SilentlyContinue
if ($null -eq $Bash) {
    throw "MSYS2 bash.exe was not found on PATH."
}

Push-Location $RepoRoot
try {
    & $Bash.Source "./build.sh"
    if ($LASTEXITCODE -ne 0) {
        throw "TapeSister build failed with exit code $LASTEXITCODE."
    }

    $PackageArguments = @{
        BuildDirectory = (Join-Path $RepoRoot "build-windows")
    }
    if (-not [string]::IsNullOrWhiteSpace($OutputDirectory)) {
        $PackageArguments.OutputDirectory = $OutputDirectory
    }
    $PackageScript = Join-Path $PSScriptRoot "package-windows-portable.ps1"
    & $PackageScript @PackageArguments
}
finally {
    Pop-Location
}
