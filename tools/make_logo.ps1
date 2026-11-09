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

# Geometry is authored in SVG; raster material is independent of the outlines.
[xml]$svg=Get-Content (Join-Path $branding 'pc-lettering.svg') -Raw
$width=[int]$svg.svg.width;$height=[int]$svg.svg.height
$hi=[System.Drawing.Bitmap]::new($width*4,$height*4,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g=[System.Drawing.Graphics]::FromImage($hi)
$g.Clear([System.Drawing.Color]::Transparent)
$g.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.ScaleTransform(4,4)
foreach($group in $svg.svg.g) {
    foreach($shape in $group.ChildNodes) {
        if($shape.NodeType -ne [System.Xml.XmlNodeType]::Element){continue}
        $points=[System.Drawing.PointF[]]@($shape.points.Split(' ',[System.StringSplitOptions]::RemoveEmptyEntries) | ForEach-Object {
            $pair=$_.Split(',')
            [System.Drawing.PointF]::new([single]::Parse($pair[0],[cultureinfo]::InvariantCulture),[single]::Parse($pair[1],[cultureinfo]::InvariantCulture))
        })
        if($shape.LocalName -eq 'polygon') {
            $g.FillPolygon([System.Drawing.Brushes]::White,$points)
        } else {
            $pen=[System.Drawing.Pen]::new([System.Drawing.Color]::White,[single]$group.'stroke-width')
            $pen.LineJoin=[System.Drawing.Drawing2D.LineJoin]::Miter
            $pen.StartCap=$pen.EndCap=[System.Drawing.Drawing2D.LineCap]::Flat
            $g.DrawLines($pen,$points);$pen.Dispose()
        }
    }
}
$g.Dispose()
$mask=[System.Drawing.Bitmap]::new($width,$height,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g=[System.Drawing.Graphics]::FromImage($mask)
$g.CompositingMode=[System.Drawing.Drawing2D.CompositingMode]::SourceCopy
$g.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.PixelOffsetMode=[System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$g.DrawImage($hi,0,0,$width,$height)
$g.Dispose();$hi.Dispose()
$maskPixels=Read-Pixels $mask
$mask.Dispose()
$texture=[System.Drawing.Bitmap]::new((Join-Path $branding 'pc-metal.png'))
$texturePixels=Read-Pixels $texture

Add-Type -TypeDefinition @'
using System;
public static class LogoMetal {
    public static byte[] Render(byte[] mask, int w, int h, byte[] texture, int tw, int th, byte[] reference, int rw) {
        var distance=new float[w*h];
        for(int i=0;i<distance.Length;i++) distance[i]=mask[i*4+3]>=128?10000:0;
        const float diagonal=1.41421356f;
        for(int y=1;y<h;y++) for(int x=1;x<w-1;x++) {
            int i=y*w+x;
            distance[i]=Math.Min(distance[i],Math.Min(Math.Min(distance[i-1]+1,distance[i-w]+1),Math.Min(distance[i-w-1]+diagonal,distance[i-w+1]+diagonal)));
        }
        for(int y=h-2;y>=0;y--) for(int x=w-2;x>=1;x--) {
            int i=y*w+x;
            distance[i]=Math.Min(distance[i],Math.Min(Math.Min(distance[i+1]+1,distance[i+w]+1),Math.Min(distance[i+w-1]+diagonal,distance[i+w+1]+diagonal)));
        }
        var result=new byte[mask.Length];
        for(int y=1;y<h-1;y++) for(int x=1;x<w-1;x++) {
            int i=y*w+x,p=i*4;
            if(mask[p+3]==0)continue;
            int tx=(x*7+y*3)%tw;
            int ty=Math.Max(0,Math.Min(th-1,(y-142)*(th-1)/203));
            int sample=(ty*tw+tx)*4;
            float d=distance[i];
            float nx=distance[i-1]-distance[i+1],ny=distance[i-w]-distance[i+w];
            float length=(float)Math.Sqrt(nx*nx+ny*ny);
            float light=length>0?(-.55f*nx-.83f*ny)/length:0;
            // N/O reference stems have a ~40px face inside a 65-69px rim.
            // Directional 8-18px bevels preserve that face width on every axis.
            float bevelWidth=13-5*light;
            float bevel=Math.Max(0,Math.Min(1,d/bevelWidth));
            int leftX=247+(int)Math.Round(bevel*9);
            int rightX=315-(int)Math.Round(bevel*20);
            float bright=(light+1)*.5f;
            for(int c=0;c<3;c++) {
                float face=texture[sample+c];
                // Reuse the actual original N stem's bevel profile; no invented
                // rounded shading or additional geometry on the original logo.
                float left=reference[(185*rw+leftX)*4+c];
                float right=reference[(185*rw+rightX)*4+c];
                float originalFace=Math.Max(1,(int)reference[(185*rw+275)*4+c]);
                float color=d>=bevelWidth?face:face*(left*bright+right*(1-bright))/originalFace;
                result[p+c]=(byte)Math.Max(0,Math.Min(255,(int)Math.Round(color)));
            }
            result[p+3]=mask[p+3];
        }
        return result;
    }
    public static void Merge(byte[] frame, byte[] letters, byte[] original, int originalWidth, int originalHeight, int width) {
        for(int p=0;p<letters.Length;p+=4) if(letters[p+3]!=0) Array.Copy(letters,p,frame,p,4);
        for(int y=0;y<originalHeight;y++) for(int x=0;x<originalWidth;x++) {
            int p=(y*originalWidth+x)*4;
            if(original[p+3]!=0)Array.Copy(original,p,frame,(y*width+x)*4,4);
        }
    }
    public static int VerifyOriginal(byte[] result, byte[] original, int originalWidth, int originalHeight, int width) {
        int count=0;
        for(int y=0;y<originalHeight;y++) for(int x=0;x<originalWidth;x++) {
            int p=(y*originalWidth+x)*4;
            if(original[p+3]==0)continue;
            int q=(y*width+x)*4;
            for(int c=0;c<4;c++)if(original[p+c]!=result[q+c])throw new Exception("Original logo pixel changed.");
            count++;
        }
        return count;
    }
}
'@
$original=[System.Drawing.Bitmap]::new((Join-Path $branding 'logo-original.png'))
$originalWidth=$original.Width;$originalHeight=$original.Height
$originalPixels=Read-Pixels $original
$letterPixels=[LogoMetal]::Render($maskPixels,$width,$height,$texturePixels,$texture.Width,$texture.Height,$originalPixels,$originalWidth)
$texture.Dispose()
$frame=[System.Drawing.Bitmap]::new((Join-Path $branding 'pc-frame.png'))
$pixels=Read-Pixels $frame
$frame.Dispose()
[LogoMetal]::Merge($pixels,$letterPixels,$originalPixels,$original.Width,$original.Height,$width)
$original.Dispose()
$output=[System.Drawing.Bitmap]::new($width,$height,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$data=$output.LockBits([System.Drawing.Rectangle]::new(0,0,$width,$height),2,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
try {
    for($y=0;$y -lt $height;$y++) {
        [System.Runtime.InteropServices.Marshal]::Copy($pixels,$y*$width*4,[IntPtr]::Add($data.Scan0,$y*$data.Stride),$width*4)
    }
} finally {$output.UnlockBits($data)}
$output.Save((Join-Path $branding 'logo-pc.png'),[System.Drawing.Imaging.ImageFormat]::Png)
$output.Dispose()
$reloaded=[System.Drawing.Bitmap]::new((Join-Path $branding 'logo-pc.png'))
$checked=[LogoMetal]::VerifyOriginal((Read-Pixels $reloaded),$originalPixels,$originalWidth,$originalHeight,$width)
$reloaded.Dispose()
Copy-Item -LiteralPath (Join-Path $branding 'logo-pc.png') -Destination (Join-Path $projectRoot 'local/launcher/logo-pc.png')
Write-Output "Rendered PC with 66px outlines and approximately 40px faces, calibrated to N/O; verified $checked original RGBA pixels unchanged."
