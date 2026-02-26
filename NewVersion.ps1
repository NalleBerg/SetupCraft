# NewVersion.ps1
# Updates version and published date across the project

param(
    [string]$PublishedDate = "",
    [string]$Version = ""
)

# Get current date/time if not provided
if ([string]::IsNullOrWhiteSpace($PublishedDate)) {
    $now = Get-Date
    $PublishedDate = $now.ToString("dd.MM.yyyy HH:mm")
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $now = Get-Date
    $Version = $now.ToString("yyyy.MM.dd.HH")
}

Write-Host "Updating version information..." -ForegroundColor Cyan
Write-Host "  Published: $PublishedDate" -ForegroundColor Green
Write-Host "  Version: $Version" -ForegroundColor Green

# Update curver.txt
$curverPath = Join-Path $PSScriptRoot "curver.txt"
$curverContent = "Published: $PublishedDate`nVersion: $Version`n"
Set-Content -Path $curverPath -Value $curverContent -NoNewline -Encoding UTF8
Write-Host "✓ Updated curver.txt" -ForegroundColor Green

# Note: Changelog.html and README.md must be updated manually
Write-Host ""
Write-Host "REMINDER: Please manually update version in:" -ForegroundColor Yellow
Write-Host "  - Changelog.html" -ForegroundColor Yellow
Write-Host "  - README.md (if applicable)" -ForegroundColor Yellow
Write-Host ""
Write-Host "Version update complete!" -ForegroundColor Green
Write-Host "Run .\makeit.bat to rebuild with new version." -ForegroundColor Cyan
