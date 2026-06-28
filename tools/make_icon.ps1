# Builds a multi-resolution .ico (PNG-compressed entries) from a source PNG.
param(
    [string]$Source = "d:\Project\!MyLightroom\LrC.png",
    [string]$Output = "d:\Project\!MyLightroom\resources\app.ico"
)

Add-Type -AssemblyName System.Drawing

$sizes = @(256, 64, 48, 32, 16)
$src = [System.Drawing.Image]::FromFile($Source)

$pngBlobs = @()
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.DrawImage($src, 0, 0, $s, $s)
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $pngBlobs += ,($ms.ToArray())
    $ms.Dispose()
}
$src.Dispose()

$outDir = Split-Path $Output
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$fs = [System.IO.File]::Create($Output)
$bw = New-Object System.IO.BinaryWriter($fs)

# ICONDIR
$bw.Write([UInt16]0)            # reserved
$bw.Write([UInt16]1)            # type = icon
$bw.Write([UInt16]$sizes.Count) # image count

# Offset where image data begins (after dir + entries).
$offset = 6 + (16 * $sizes.Count)
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $blob = $pngBlobs[$i]
    $dim = if ($s -ge 256) { [byte]0 } else { [byte]$s }
    $bw.Write([byte]$dim)        # width  (0 => 256)
    $bw.Write([byte]$dim)        # height (0 => 256)
    $bw.Write([byte]0)           # palette count
    $bw.Write([byte]0)           # reserved
    $bw.Write([UInt16]1)         # color planes
    $bw.Write([UInt16]32)        # bits per pixel
    $bw.Write([UInt32]$blob.Length)
    $bw.Write([UInt32]$offset)
    $offset += $blob.Length
}
foreach ($blob in $pngBlobs) { $bw.Write($blob) }

$bw.Flush()
$bw.Close()
$fs.Close()
Write-Host "Wrote $Output ($((Get-Item $Output).Length) bytes)"
