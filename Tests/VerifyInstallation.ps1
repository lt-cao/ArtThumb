$ErrorActionPreference = "Stop"

$provider = "{A0654CAE-E1AC-4A73-96F8-CB5453EE3264}"
$handler = "{E357FCCD-A995-4576-B01F-234630154E96}"
$extensions = ".psd", ".psb", ".ai", ".eps", ".indd", ".pdf", ".svg"

$clsidPath = "Registry::HKEY_CURRENT_USER\Software\Classes\CLSID\$provider\InprocServer32"
$dll = (Get-ItemProperty -LiteralPath $clsidPath).'(default)'
if (-not (Test-Path -LiteralPath $dll)) {
    throw "Provider DLL does not exist: $dll"
}

$product = Get-ItemProperty -LiteralPath "Registry::HKEY_CURRENT_USER\Software\ArtThumb"
$settings = $product.SettingsPath
if (-not (Test-Path -LiteralPath $settings)) {
    throw "Settings executable does not exist: $settings"
}

foreach ($extension in $extensions) {
    $path = "Registry::HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\$extension\ShellEx\$handler"
    $value = (Get-ItemProperty -LiteralPath $path).'(default)'
    if ($value -ne $provider) {
        throw "$extension is registered to '$value', expected '$provider'"
    }

    $backupPath = "Registry::HKEY_CURRENT_USER\Software\ArtThumb\Backups\$extension"
    $overlayTarget = (Get-ItemProperty -LiteralPath $backupPath).OverlayTarget
    $overlayPath = "Registry::HKEY_CURRENT_USER\$overlayTarget"
    $overlay = (Get-ItemProperty -LiteralPath $overlayPath -ErrorAction SilentlyContinue).TypeOverlay
    if ($null -ne $overlay) {
        throw "Explorer TypeOverlay should be absent so its native app icon is used for $extension"
    }
}

Write-Host "ArtThumb registration and native app thumbnail overlays are valid for all seven extensions."
Write-Host "Provider: $dll"
Write-Host "Settings: $settings"
