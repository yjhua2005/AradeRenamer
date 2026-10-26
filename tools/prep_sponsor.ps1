# AradeRenamer 收款码预处理：去元数据 + 缩放 + 统一压缩参数，产物打进 exe（RCDATA）。
# 用法（工程根目录或任意位置）：powershell -NoProfile -ExecutionPolicy Bypass -File tools\prep_sponsor.ps1
# 换图：覆盖 REF\WeChat.png / REF\Alipay.jpg / REF\PayPal.jpg 后重跑本脚本，再重新 build。
param([switch]$SkipDocs)
$ErrorActionPreference = 'Stop'
$root   = Split-Path -Parent $PSScriptRoot
$magick = if (Test-Path 'C:\tools\magick.exe') { 'C:\tools\magick.exe' } else { (Get-Command magick.exe -ErrorAction SilentlyContinue).Source }
if (-not $magick) { Write-Error 'magick.exe 未找到（预期 C:\tools\magick.exe），请先安装 ImageMagick'; exit 1 }

$jobs = @(
    @{ src = 'REF\WeChat.png'; name = 'sponsor_wechat'; label = '微信'  },
    @{ src = 'REF\Alipay.jpg'; name = 'sponsor_alipay'; label = '支付宝' },
    @{ src = 'REF\PayPal.jpg'; name = 'sponsor_paypal'; label = 'PayPal' }
)

# -strip: 去掉 EXIF/ICC/缩略图；-resize 720x720: 只缩不放；PNG 先转平到白底再压 JPEG。
foreach ($j in $jobs) {
    $in = Join-Path $root $j.src
    if (-not (Test-Path $in)) { Write-Error "源图不存在: $in"; exit 1 }
    $dsts = @(Join-Path $root "resources\$($j.name).jpg")
    if (-not $SkipDocs) {
        $docs = Join-Path $root "docs\sponsor\$($j.name).jpg"
        New-Item -ItemType Directory -Force -Path (Split-Path $docs) | Out-Null
        $dsts += $docs
    }
    # magick 不支持一次写多个同名产物（会按场景编号），所以每个目标各跑一次。
    foreach ($d in $dsts) {
        & $magick $in -strip -interlace Plane -background white -flatten -resize 720x720 -quality 85 $d
        if ($LASTEXITCODE -ne 0) { Write-Error "magick 处理失败: $($j.src) -> $d"; exit 1 }
    }
    $di = & $magick identify -format '%wx%h %b' $dsts[0]
    Write-Host ("{0,-7} -> {1}  {2}" -f $j.label, ($dsts -join ', '), $di)
}
Write-Host '完成。接下来运行 build.cmd 重新编译，把新图打进 exe。'
