$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# Draw the TM monogram as vectors so every Windows icon size is antialiased.
function New-IconImage([int]$size) {
    $bitmap = [System.Drawing.Bitmap]::new($size * 4, $size * 4)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = 'AntiAlias'
    $graphics.ScaleTransform($size * 4 / 128.0, $size * 4 / 128.0)
    $blue = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        [System.Drawing.Point]::new(0, 40), [System.Drawing.Point]::new(0, 90),
        [System.Drawing.Color]::FromArgb(0, 190, 255), [System.Drawing.Color]::FromArgb(15, 57, 220))
    $navy = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(29, 49, 76))
    $pointsT = [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new(12, 41), [System.Drawing.PointF]::new(51, 41),
        [System.Drawing.PointF]::new(48, 54), [System.Drawing.PointF]::new(36, 54),
        [System.Drawing.PointF]::new(28, 89), [System.Drawing.PointF]::new(12, 89),
        [System.Drawing.PointF]::new(20, 54), [System.Drawing.PointF]::new(9, 54))
    $pointsM = [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new(50, 41), [System.Drawing.PointF]::new(65, 41),
        [System.Drawing.PointF]::new(75, 57), [System.Drawing.PointF]::new(90, 41),
        [System.Drawing.PointF]::new(107, 41), [System.Drawing.PointF]::new(97, 89),
        [System.Drawing.PointF]::new(81, 89), [System.Drawing.PointF]::new(87, 62),
        [System.Drawing.PointF]::new(73, 76), [System.Drawing.PointF]::new(64, 61),
        [System.Drawing.PointF]::new(58, 89), [System.Drawing.PointF]::new(40, 89))
    $graphics.FillPolygon($blue, $pointsT)
    $graphics.FillPolygon($navy, $pointsM)
    $swoosh = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $swoosh.AddBezier(43, 63, 26, 82, 63, 86, 89, 76)
    $swoosh.AddBezier(89, 76, 92, 69, 91, 58, 93, 53)
    $swoosh.AddBezier(93, 53, 95, 51, 97, 51, 99, 50)
    $swoosh.AddBezier(99, 50, 113, 52, 123, 58, 120, 65)
    $swoosh.AddBezier(120, 65, 114, 77, 71, 88, 50, 85)
    $swoosh.AddBezier(50, 85, 29, 83, 29, 72, 43, 63)
    $swoosh.CloseFigure()
    $outline = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(12, 26, 49), 1.5)
    $graphics.FillPath($blue, $swoosh)
    $graphics.DrawPath($outline, $swoosh)
    $result = [System.Drawing.Bitmap]::new($size, $size)
    $target = [System.Drawing.Graphics]::FromImage($result)
    $target.InterpolationMode = 'HighQualityBicubic'
    $target.DrawImage($bitmap, 0, 0, $size, $size)
    $target.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
    $blue.Dispose(); $navy.Dispose(); $swoosh.Dispose(); $outline.Dispose()
    return $result
}

$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$images = @($sizes | ForEach-Object {
    $bitmap = New-IconImage $_
    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    if ($_ -eq 256) { $bitmap.Save((Join-Path $PSScriptRoot 'tpmate.png')) }
    ,$stream.ToArray()
    $stream.Dispose(); $bitmap.Dispose()
})
$file = [System.IO.File]::Create((Join-Path $PSScriptRoot 'tpmate.ico'))
$writer = [System.IO.BinaryWriter]::new($file)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($index = 0; $index -lt $sizes.Count; $index++) {
    $dimension = $sizes[$index] % 256
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
    $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32)
    $writer.Write([uint32]$images[$index].Length); $writer.Write([uint32]$offset)
    $offset += $images[$index].Length
}
foreach ($bytes in $images) { $writer.Write([byte[]]$bytes) }
$writer.Dispose()
