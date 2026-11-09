param()
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot=Split-Path $PSScriptRoot -Parent
$branding=Join-Path $projectRoot 'assets/branding'

function Read-Pixels($image) {
    $rect=[System.Drawing.Rectangle]::new(0,0,$image.Width,$image.Height)
    $data=$image.LockBits($rect,1,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $pixels=[byte[]]::new($image.Width*$image.Height*4)
    try {
        for($y=0;$y -lt $image.Height;$y++) {
            [System.Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($data.Scan0,$y*$data.Stride),$pixels,$y*$image.Width*4,$image.Width*4)
        }
    } finally {$image.UnlockBits($data)}
    return ,$pixels
}

# Use the owner's exact raster attachment; only proportional placement is applied.
$raster=[System.Drawing.Bitmap]::new((Join-Path $branding 'pc-raster.png'))
$canvas=[System.Drawing.Bitmap]::new(2000,470,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g=[System.Drawing.Graphics]::FromImage($canvas)
$g.Clear([System.Drawing.Color]::FromArgb(0,0,0,0))
$g.CompositingMode=[System.Drawing.Drawing2D.CompositingMode]::SourceCopy
$g.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.PixelOffsetMode=[System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$placed=[System.Drawing.RectangleF]::new(1280,60,594,[single](594*$raster.Height/$raster.Width))
$g.DrawImage($raster,$placed)
$g.Dispose();$raster.Dispose()
$pixels=Read-Pixels $canvas
$original=[System.Drawing.Bitmap]::new((Join-Path $branding 'logo-original.png'))
$originalPixels=Read-Pixels $original
$originalWidth=$original.Width;$originalHeight=$original.Height
$original.Dispose()
Add-Type @'
using System;
public static class OriginalLogoPixels {
    public static void Copy(byte[] output,byte[] original,int originalWidth,int originalHeight,int outputWidth) {
        for(int y=0;y<originalHeight;y++)for(int x=0;x<originalWidth;x++) {
            int p=(y*originalWidth+x)*4;
            if(original[p+3]!=0)Array.Copy(original,p,output,(y*outputWidth+x)*4,4);
        }
    }
    public static int Verify(byte[] output,byte[] original,int originalWidth,int originalHeight,int outputWidth) {
        int count=0;
        for(int y=0;y<originalHeight;y++)for(int x=0;x<originalWidth;x++) {
            int p=(y*originalWidth+x)*4;
            if(original[p+3]==0)continue;
            int q=(y*outputWidth+x)*4;
            for(int c=0;c<4;c++)if(original[p+c]!=output[q+c])throw new Exception("Original logo pixel changed.");
            count++;
        }
        return count;
    }
}
'@
[OriginalLogoPixels]::Copy($pixels,$originalPixels,$originalWidth,$originalHeight,2000)
$data=$canvas.LockBits([System.Drawing.Rectangle]::new(0,0,2000,470),2,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
try {
    for($y=0;$y -lt 470;$y++) {
        [System.Runtime.InteropServices.Marshal]::Copy($pixels,$y*8000,[IntPtr]::Add($data.Scan0,$y*$data.Stride),8000)
    }
} finally {$canvas.UnlockBits($data)}
$canvas.Save((Join-Path $branding 'logo-pc.png'),[System.Drawing.Imaging.ImageFormat]::Png)
$canvas.Dispose()
$reloaded=[System.Drawing.Bitmap]::new((Join-Path $branding 'logo-pc.png'))
$verified=[OriginalLogoPixels]::Verify((Read-Pixels $reloaded),$originalPixels,$originalWidth,$originalHeight,2000)
$reloaded.Dispose()
Copy-Item -LiteralPath (Join-Path $branding 'logo-pc.png') -Destination (Join-Path $projectRoot 'local/launcher/logo-pc.png')
Write-Output "Composited the finished PC raster; verified $verified original RGBA pixels unchanged."
