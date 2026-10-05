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
$updater = $product.UpdaterPath
if (-not (Test-Path -LiteralPath $updater)) {
    throw "Update checker does not exist: $updater"
}
$startMenu = Join-Path ([Environment]::GetFolderPath("Programs")) "ArtThumb\ArtThumb Update.lnk"
if (-not (Test-Path -LiteralPath $startMenu)) {
    throw "ArtThumb Update shortcut does not exist: $startMenu"
}
if ($null -ne $product.SettingsPath) {
    throw "Legacy settings registry value should have been removed"
}
if (Test-Path -LiteralPath "Registry::HKEY_CURRENT_USER\Software\ArtThumb\Settings") {
    throw "Legacy sharpness settings should have been removed"
}

foreach ($extension in $extensions) {
    $path = "Registry::HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\$extension\ShellEx\$handler"
    $value = (Get-ItemProperty -LiteralPath $path).'(default)'
    if ($value -ne $provider) {
        throw "$extension is registered to '$value', expected '$provider'"
    }

    $backupPath = "Registry::HKEY_CURRENT_USER\Software\ArtThumb\Backups\$extension"
    $backup = Get-ItemProperty -LiteralPath $backupPath
    $overlayTarget = $backup.OverlayTarget
    $overlayPath = "Registry::HKEY_CURRENT_USER\$overlayTarget"
    $overlay = (Get-ItemProperty -LiteralPath $overlayPath -ErrorAction SilentlyContinue).TypeOverlay
    if ($backup.InstalledOverlayPresent -eq 1) {
        if ([string]::IsNullOrWhiteSpace($backup.InstalledOverlay) -or
            $overlay -ne $backup.InstalledOverlay) {
            throw "Explorer TypeOverlay should use the associated app icon for $extension"
        }
    } elseif ($null -ne $overlay) {
        throw "Unexpected ArtThumb TypeOverlay value for $extension without an associated icon"
    }
}

Write-Host "ArtThumb registration and native app thumbnail overlays are valid for all seven extensions."
Write-Host "Provider: $dll"
Write-Host "Update checker: $updater"
Write-Host "Start Menu shortcut: $startMenu"
