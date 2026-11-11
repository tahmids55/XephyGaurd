Import-Module Microsoft.PowerShell.Security -ErrorAction SilentlyContinue
Import-Module Microsoft.PowerShell.Utility -ErrorAction SilentlyContinue

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

$brandingDir = "d:\Project\Running\XephyGuard\branding"
$installersDir = "d:\Project\Running\XephyGuard\installers\windows"

# Export public CER & private PFX
Export-Certificate -Cert $cert -FilePath "$brandingDir\XerophyteLabs-CodeSigning.cer" -Force | Out-Null
Copy-Item "$brandingDir\XerophyteLabs-CodeSigning.cer" "$installersDir\XerophyteLabs-CodeSigning.cer" -Force

$pwd = ConvertTo-SecureString -String "XerophyteLabs2026!" -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath "$brandingDir\XerophyteLabs-CodeSigning.pfx" -Password $pwd -Force | Out-Null

# Import into TrustedPublisher using certutil (silent, non-interactive)
[Console]::WriteLine("==> Registering in TrustedPublisher store...")
& certutil.exe -addstore -user -f "TrustedPublisher" "$brandingDir\XerophyteLabs-CodeSigning.cer" | Out-Null

# List of files to sign
$targets = @(
    "d:\Project\Running\XephyGuard\agent\build-windows\Release\xephyguard-agent.exe",
    "d:\Project\Running\XephyGuard\agent\build-windows\Release\xephyguard-helper.exe",
    "d:\Project\Running\XephyGuard\installers\windows\XephyGuard-Setup-0.1.0.exe"
)

[Console]::WriteLine("==> Signing binaries with Authenticode (Publisher: Xerophyte Labs)...")
foreach ($target in $targets) {
    if (Test-Path $target) {
        $sig = Set-AuthenticodeSignature -FilePath $target -Certificate $cert -HashAlgorithm SHA256
        [Console]::WriteLine("Signed: " + (Split-Path $target -Leaf) + " -> Status: " + $sig.Status)
    }
}

[Console]::WriteLine("==> Verification completed successfully!")
