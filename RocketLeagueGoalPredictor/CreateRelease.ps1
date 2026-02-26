$ZipName = "GoalPredictor.zip"

$TempId = [Guid]::NewGuid().ToString().Substring(0,8)
$TempPath = Join-Path $env:TEMP "GP_Build_$TempId"

Write-Host "Creating temporary workspace..." -ForegroundColor Cyan
New-Item -ItemType Directory -Path (Join-Path $TempPath "data") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $TempPath "plugins") -Force | Out-Null

Write-Host "Staging files..." -ForegroundColor Cyan
Copy-Item -Path "data" -Destination $TempPath -Recurse -Force
Copy-Item -Path "plugins\GoalPredictor.dll" -Destination (Join-Path $TempPath "plugins") -Force
Copy-Item -Path "Release_README.txt" -Destination (Join-Path $TempPath "README.txt") -Force

Write-Host "Compressing archive..." -ForegroundColor Cyan
if (Test-Path $ZipName) { Remove-Item $ZipName -Force }
Compress-Archive -Path "$TempPath\*" -DestinationPath "./$ZipName" -CompressionLevel Optimal

Write-Host "Cleaning up temporary files..." -ForegroundColor Cyan
Remove-Item -Path $TempPath -Recurse -Force

Write-Host "Success! Created $ZipName." -ForegroundColor Green