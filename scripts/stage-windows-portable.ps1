[CmdletBinding()]
param(
    [string]$BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string]$Destination
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $RepoRoot "build-windows"
}
$BuildPath = [System.IO.Path]::GetFullPath($BuildDirectory)
$DestinationPath = [System.IO.Path]::GetFullPath($Destination)

if (-not (Test-Path -LiteralPath $BuildPath -PathType Container)) {
    throw "TapeSister build directory not found: $BuildPath"
}

New-Item -ItemType Directory -Path $DestinationPath -Force | Out-Null

function Copy-PortableFile {
    param(
        [Parameter(Mandatory = $true)] [string]$Source,
        [Parameter(Mandatory = $true)] [string]$Name
    )

    $SourcePath = [System.IO.Path]::GetFullPath($Source)
    if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
        throw "Missing required TapeSister portable file: $SourcePath"
    }
    Copy-Item -LiteralPath $SourcePath `
        -Destination (Join-Path $DestinationPath $Name) -Force
}

function Copy-PortableDirectory {
    param(
        [Parameter(Mandatory = $true)] [string]$Source,
        [Parameter(Mandatory = $true)] [string]$Name
    )

    $SourcePath = [System.IO.Path]::GetFullPath($Source)
    if (-not (Test-Path -LiteralPath $SourcePath -PathType Container)) {
        throw "Missing required TapeSister portable directory: $SourcePath"
    }
    Copy-Item -LiteralPath $SourcePath `
        -Destination (Join-Path $DestinationPath $Name) -Recurse -Force
}

$Executable = Join-Path $BuildPath "TapeSister.exe"
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    $Executable = Join-Path $BuildPath "tapesister.exe"
}
Copy-PortableFile -Source $Executable -Name "TapeSister.exe"

$RequiredDlls = @("SDL2.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
foreach ($Dll in $RequiredDlls) {
    Copy-PortableFile -Source (Join-Path $BuildPath $Dll) -Name $Dll
}

$GccRuntime = Get-ChildItem -LiteralPath $BuildPath -Filter "libgcc_s_*.dll" `
    -File | Select-Object -First 1
if ($null -eq $GccRuntime) {
    throw "No MinGW libgcc runtime DLL was found in $BuildPath"
}
Copy-PortableFile -Source $GccRuntime.FullName -Name $GccRuntime.Name

Copy-PortableDirectory -Source (Join-Path $BuildPath "assets") -Name "assets"
Copy-PortableDirectory -Source (Join-Path $BuildPath "cdp") -Name "cdp"
Copy-PortableDirectory -Source (Join-Path $BuildPath "licenses") -Name "licenses"
Copy-PortableFile -Source (Join-Path $BuildPath "tapesister.ini.example") `
    -Name "tapesister.ini.example"
Copy-PortableFile -Source (Join-Path $RepoRoot "release/PORTABLE_README.txt") `
    -Name "README.txt"
Copy-PortableFile -Source (Join-Path $RepoRoot "THIRD_PARTY_NOTICES.md") `
    -Name "THIRD_PARTY_NOTICES.md"

$Documentation = Join-Path $DestinationPath "docs"
New-Item -ItemType Directory -Path $Documentation -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $RepoRoot "docs/QUICK_REFERENCE.md") `
    -Destination $Documentation -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "docs/USER_MANUAL.md") `
    -Destination $Documentation -Force

$RequiredLayout = @(
    "TapeSister.exe",
    "SDL2.dll",
    "assets/tapesister_splash.png",
    "assets/palette.pal",
    "cdp/bin",
    "licenses/THIRD_PARTY_NOTICES.md",
    "tapesister.ini.example",
    "README.txt"
)
foreach ($RelativePath in $RequiredLayout) {
    if (-not (Test-Path -LiteralPath (Join-Path $DestinationPath $RelativePath))) {
        throw "Portable package validation failed; missing $RelativePath"
    }
}

Write-Host "TapeSister portable runtime staged in '$DestinationPath'."
