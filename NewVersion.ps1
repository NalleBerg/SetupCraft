# NewVersion.ps1
# Updates version and published date across the project

param(
    [string]$PublishedDate = "",
    [string]$Version = ""
)

function Get-NowStrings {
    $n = Get-Date
    return @{ Published = $n.ToString('dd.MM.yyyy HH:mm'); Version = $n.ToString('yyyy.MM.dd.HH') }
}

if ([string]::IsNullOrWhiteSpace($PublishedDate) -or [string]::IsNullOrWhiteSpace($Version)) {
    $now = Get-NowStrings
    if ([string]::IsNullOrWhiteSpace($PublishedDate)) { $PublishedDate = $now.Published }
    if ([string]::IsNullOrWhiteSpace($Version)) { $Version = $now.Version }
}

Write-Host 'Updating version information...' -ForegroundColor Cyan
Write-Host "  Published: $PublishedDate" -ForegroundColor Green
Write-Host "  Version: $Version" -ForegroundColor Green

# Update curver.txt (write two lines)
$curverPath = Join-Path $PSScriptRoot 'curver.txt'
$lines = @("Published: $PublishedDate", "Version: $Version")
$lines | Out-File -FilePath $curverPath -Encoding UTF8
Write-Host '✓ Updated curver.txt' -ForegroundColor Green

Write-Host ''
Write-Host 'REMINDER: Please manually update version in:' -ForegroundColor Yellow
Write-Host '  - Changelog.html' -ForegroundColor Yellow
Write-Host '  - README.md (if applicable)' -ForegroundColor Yellow
Write-Host ''
Write-Host 'Version update complete!' -ForegroundColor Green
Write-Host 'Run .\makeit.bat to rebuild with new version.' -ForegroundColor Cyan
