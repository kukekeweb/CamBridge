[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]] $IPAddress,

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string] $OutputDirectory = "windows/stage1-server/certificates",

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string] $CommonName = "cambridge.local"
)

$ErrorActionPreference = "Stop"
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

$sanEntries = @("DNS=$CommonName") + ($IPAddress | ForEach-Object { "IPAddress=$($_)" })
$sanValue = $sanEntries -join "&"

$root = New-SelfSignedCertificate `
    -Type Custom `
    -Subject "CN=CamBridge Local CA" `
    -KeyAlgorithm RSA `
    -KeyLength 3072 `
    -HashAlgorithm SHA256 `
    -KeyExportPolicy Exportable `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -NotAfter (Get-Date).AddYears(5) `
    -KeyUsage CertSign, CRLSign `
    -TextExtension "2.5.29.19={critical}{text}CA=true&pathlength=1"

$server = New-SelfSignedCertificate `
    -Type Custom `
    -Subject "CN=$CommonName" `
    -Signer $root `
    -KeyAlgorithm RSA `
    -KeyLength 2048 `
    -HashAlgorithm SHA256 `
    -KeyExportPolicy Exportable `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -NotAfter (Get-Date).AddYears(2) `
    -TextExtension @(
        "2.5.29.17={text}$sanValue",
        "2.5.29.19={critical}{text}CA=false",
        "2.5.29.37={text}1.3.6.1.5.5.7.3.1"
    )

Export-Certificate -Cert $root -FilePath (Join-Path $outputPath "cambridge-root-ca.cer") -Type CERT | Out-Null
Export-Certificate -Cert $server -FilePath (Join-Path $outputPath "cambridge-server.cer") -Type CERT | Out-Null

$pfxPassword = Read-Host "Enter a password for cambridge-server.pfx (not stored by this script)" -AsSecureString
Export-PfxCertificate `
    -Cert $server `
    -FilePath (Join-Path $outputPath "cambridge-server.pfx") `
    -Password $pfxPassword `
    -CryptoAlgorithmOption AES256_SHA256 | Out-Null

Write-Output "Created public root certificate: $(Join-Path $outputPath 'cambridge-root-ca.cer')"
Write-Output "Created public server certificate: $(Join-Path $outputPath 'cambridge-server.cer')"
Write-Output "Created password-protected server PFX: $(Join-Path $outputPath 'cambridge-server.pfx')"
Write-Output "Certificate SAN: $sanValue"
Write-Output "The PFX password was not written to disk or output."
