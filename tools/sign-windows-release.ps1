param(
    [Parameter(Mandatory = $true)]
    [string] $StagingDirectory,

    [Parameter(Mandatory = $true)]
    [string] $CertificatePath,

    [string] $TimestampUrl = "https://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"

$staging = (Resolve-Path -LiteralPath $StagingDirectory).Path
$certificate = (Resolve-Path -LiteralPath $CertificatePath).Path
$password = $env:WINDOWS_SIGNING_PFX_PASSWORD

if ([string]::IsNullOrWhiteSpace($password)) {
    throw "WINDOWS_SIGNING_PFX_PASSWORD is required"
}

$signTool = Get-Command "signtool.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty Source

if (-not $signTool) {
    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    if (Test-Path -LiteralPath $kitsRoot) {
        $signTool = Get-ChildItem -LiteralPath $kitsRoot -Recurse -File -Filter "signtool.exe" |
            Where-Object { $_.FullName -match '[\\/]x64[\\/]signtool\.exe$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    }
}

if (-not $signTool) {
    throw "signtool.exe was not found"
}

$expectedRelativePaths = @(
    "DirectPipe.exe",
    "DirectPipe Receiver.dll",
    "DirectPipe Receiver.vst3\Contents\x86_64-win\DirectPipe Receiver.vst3"
)
$releaseBinaries = @(
    foreach ($relativePath in $expectedRelativePaths) {
        $binaryPath = Join-Path $staging $relativePath
        if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) {
            throw "Required Windows release binary is missing: $relativePath"
        }
        Get-Item -LiteralPath $binaryPath
    }
)
$allReleaseBinaries = @(
    Get-ChildItem -LiteralPath $staging -Recurse -File |
        Where-Object {
            $_.Extension.ToLowerInvariant() -in ".exe", ".dll", ".vst3"
        }
)

if ($allReleaseBinaries.Count -ne $releaseBinaries.Count) {
    throw "Expected exactly DirectPipe.exe and Receiver VST2/VST3 binaries; found $($allReleaseBinaries.Count)"
}

foreach ($binary in $releaseBinaries) {
    $existingSignature =
        Get-AuthenticodeSignature -LiteralPath $binary.FullName
    if ($existingSignature.Status -notin @(
            [System.Management.Automation.SignatureStatus]::Valid,
            [System.Management.Automation.SignatureStatus]::NotSigned)) {
        throw "Refusing to replace an invalid existing Authenticode signature on $($binary.FullName): $($existingSignature.Status)"
    }
}

foreach ($binary in $releaseBinaries) {
    Write-Host "Signing $($binary.FullName.Substring($staging.Length + 1))"
    & $signTool sign `
        /fd SHA256 `
        /td SHA256 `
        /tr $TimestampUrl `
        /f $certificate `
        /p $password `
        $binary.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "signtool sign failed for $($binary.FullName)"
    }

    & $signTool verify /pa /all $binary.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "signtool verify failed for $($binary.FullName)"
    }

    $signature = Get-AuthenticodeSignature -LiteralPath $binary.FullName
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "Authenticode verification failed for $($binary.FullName): $($signature.Status)"
    }

    Write-Host "Verified $($binary.Name): $($signature.SignerCertificate.Subject)"
}

Write-Host "All $($releaseBinaries.Count) Windows release binaries are Authenticode signed and valid."
