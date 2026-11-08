# 把 gfx_gallery.exe 记录的标注（docs/images/captions.txt）用系统中文字体叠到图上，
# 同时把 BMP 转成 PNG。
# 用法: powershell -ExecutionPolicy Bypass -File tools\render_captions.ps1

param([string]$Root = "m:\ESDBox")

Add-Type -AssemblyName System.Drawing

$imgDir = Join-Path $Root "docs\images"
$capFile = Join-Path $Root "tools\captions.txt"

if (-not (Test-Path $capFile)) {
    Write-Error "找不到 $capFile，先运行 build\gfx_gallery.exe"
    exit 1
}

# 优先用雅黑，没有就退回黑体
$families = [System.Drawing.FontFamily]::Families | ForEach-Object { $_.Name }
$fontName = "Microsoft YaHei UI"
foreach ($f in @("Microsoft YaHei UI", "Microsoft YaHei", "SimHei", "SimSun")) {
    if ($families -contains $f) { $fontName = $f; break }
}
Write-Host "caption font: $fontName"

$groups = @{}
foreach ($line in (Get-Content -Path $capFile -Encoding UTF8)) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $p = $line -split '\|', 7
    if ($p.Count -lt 7) { continue }
    if (-not $groups.ContainsKey($p[0])) {
        $groups[$p[0]] = New-Object System.Collections.ArrayList
    }
    [void]$groups[$p[0]].Add($p)
}

$fmt = [System.Drawing.StringFormat]::GenericTypographic

foreach ($name in $groups.Keys) {
    $bmpPath = Join-Path $imgDir "$name.bmp"
    if (-not (Test-Path $bmpPath)) {
        Write-Warning "缺少 $bmpPath"
        continue
    }

    $bmp = [System.Drawing.Bitmap]::FromFile($bmpPath)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias

    foreach ($p in $groups[$name]) {
        $size   = [int]$p[1]
        $x      = [int]$p[2]
        $y      = [int]$p[3]
        $center = ($p[4] -eq '1')
        $rgb    = $p[5]
        $text   = $p[6]

        $style = if ($size -ge 20) {
            [System.Drawing.FontStyle]::Bold
        } else {
            [System.Drawing.FontStyle]::Regular
        }

        $font = New-Object System.Drawing.Font($fontName, [float]$size, $style,
                                               [System.Drawing.GraphicsUnit]::Pixel)
        $color = [System.Drawing.Color]::FromArgb(
            255,
            [Convert]::ToInt32($rgb.Substring(0, 2), 16),
            [Convert]::ToInt32($rgb.Substring(2, 2), 16),
            [Convert]::ToInt32($rgb.Substring(4, 2), 16))
        $brush = New-Object System.Drawing.SolidBrush($color)

        $drawX = [single]$x
        if ($center) {
            $sz = $g.MeasureString($text, $font, [System.Drawing.PointF]::new(0, 0), $fmt)
            $drawX = [single]($x - $sz.Width / 2)
        }

        $g.DrawString($text, $font, $brush, $drawX, [single]$y, $fmt)

        $brush.Dispose()
        $font.Dispose()
    }

    $pngPath = Join-Path $imgDir "$name.png"
    $bmp.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
    Remove-Item $bmpPath
    Write-Host "  -> $pngPath"
}
