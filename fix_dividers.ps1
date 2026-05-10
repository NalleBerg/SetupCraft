$enc = New-Object System.Text.UTF8Encoding($false)
$cpp = "C:\Users\NalleBerg\Documents\C++\Workspace\SetupCraft\page_manual.cpp"
$content = [System.IO.File]::ReadAllText($cpp, $enc)
$before = ($content.Split("RGB(100,140,180));").Length - 1)
$content = $content.Replace("RGB(100,140,180));", "RGB(100,140,180), 0, true);")
[System.IO.File]::WriteAllText($cpp, $content, $enc)
$after = ($content.Split("RGB(100,140,180));").Length - 1)
Write-Host "Fixed $before instances. Remaining: $after"
