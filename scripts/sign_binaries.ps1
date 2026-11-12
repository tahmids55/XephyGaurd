param(
    [string]$Version = "",
    [string]$PfxPassword = $env:XG_PFX_PASSWORD
)

Import-Module Microsoft.PowerShell.Security -ErrorAction SilentlyContinue
Import-Module Microsoft.PowerShell.Utility -ErrorAction SilentlyContinue

$repoRoot = Split-Path -Parent $PSScriptRoot
$brandingDir = Join-Path $repoRoot "branding"
$installersDir = Join-Path $repoRoot "installers\windows"
$releaseDir = Join-Path $repoRoot "agent\build-windows\Release"

[Console]::WriteLine("==> Finding or creating Xerophyte Labs Code Signing Certificate...")

$cert = Get-ChildItem -Path Cert:\CurrentUser\My -CodeSigningCert -ErrorAction SilentlyContinue | Where-Object { $_.Subject -like "*Xerophyte Labs*" } | Select-Object -First 1

if (-not $cert) {
    [Console]::WriteLine("==> Generating new Code Signing Certificate for 'Xerophyte Labs'...")
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject "CN=Xerophyte Labs, O=Xerophyte Labs, OU=Academic Security, C=BD" `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -NotAfter (Get-Date).AddYears(10) `
        -KeyExportPolicy Exportable `
        -KeyLength 2048 `
        -HashAlgorithm SHA256
}

[Console]::WriteLine("Certificate: " + $cert.Subject)
[Console]::WriteLine("Thumbprint:  " + $cert.Thumbprint)

# Export public CER & private PFX
Export-Certificate -Cert $cert -FilePath "$brandingDir\XerophyteLabs-CodeSigning.cer" -Force | Out-Null
Copy-Item "$brandingDir\XerophyteLabs-CodeSigning.cer" "$installersDir\XerophyteLabs-CodeSigning.cer" -Force

if (-not $PfxPassword) {
    if ([Environment]::UserInteractive) {
        $pwd = Read-Host -Prompt "Enter code-signing PFX export password" -AsSecureString
    } else {
        throw "XG_PFX_PASSWORD environment variable or -PfxPassword parameter is required in non-interactive mode."
    }
} else {
    $pwd = ConvertTo-SecureString -String $PfxPassword -Force -AsPlainText
}
Export-PfxCertificate -Cert $cert -FilePath "$brandingDir\XerophyteLabs-CodeSigning.pfx" -Password $pwd -Force | Out-Null

# Import into TrustedPublisher using certutil (silent, non-interactive)
[Console]::WriteLine("==> Registering in TrustedPublisher store...")
& certutil.exe -addstore -user -f "TrustedPublisher" "$brandingDir\XerophyteLabs-CodeSigning.cer" | Out-Null

# List of files to sign (dynamically resolved relative to repo root)
$targets = @(
    (Join-Path $releaseDir "xephyguard-agent.exe"),
    (Join-Path $releaseDir "xephyguard-helper.exe")
)
$setupPattern = if ($Version) { "XephyGuard-Setup-$Version.exe" } else { "XephyGuard-Setup*.exe" }
$setupTargets = Get-ChildItem -Path $installersDir -Filter $setupPattern -File -ErrorAction SilentlyContinue
foreach ($s in $setupTargets) {
    $targets += $s.FullName
}

[Console]::WriteLine("==> Signing binaries with Authenticode (Publisher: Xerophyte Labs)...")
foreach ($target in $targets) {
    if (Test-Path $target) {
        $sig = Set-AuthenticodeSignature -FilePath $target -Certificate $cert -HashAlgorithm SHA256
        [Console]::WriteLine("Signed: " + (Split-Path $target -Leaf) + " -> Status: " + $sig.Status)
    }
}

[Console]::WriteLine("==> Verification completed successfully!")
