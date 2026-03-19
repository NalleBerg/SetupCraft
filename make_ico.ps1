param(
    [Parameter(Mandatory)][string]$Source,
    [string]$Output = ""
)

if (-not (Test-Path $Source)) {
    Write-Error "File not found: $Source"
    exit 1
}

if ($Output -eq "") {
    $Output = [System.IO.Path]::ChangeExtension($Source, $null).TrimEnd('.') + "_multi.ico"
}

$sizes = @(16, 24, 32, 40, 48, 64, 96, 128)

# Build the resize arguments: each size produces one frame in the output .ico
$args = @($Source)
foreach ($sz in $sizes) {
    $args += "("
    $args += "+clone"
    $args += "-resize"
    $args += "${sz}x${sz}!"
    $args += ")"
}
$args += "-delete"
$args += "0"
$args += $Output

magick @args

if ($LASTEXITCODE -eq 0) {
    Write-Host "Written: $Output  ($($sizes -join ', ') px)"
} else {
    Write-Error "ImageMagick failed (exit $LASTEXITCODE)"
}
