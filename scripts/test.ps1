$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$binaryDirectory = Join-Path $projectRoot 'build'
$testDirectory = Join-Path $binaryDirectory ('test-results-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null

$selfTestReport = Join-Path $testDirectory 'screenshot-self-test.txt'
$process = Start-Process -FilePath (Join-Path $binaryDirectory 'LightCapture.exe') -ArgumentList ('--self-test "' + $selfTestReport + '"') -WindowStyle Hidden -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "Screenshot self-test failed: $($process.ExitCode)" }
Get-Content -LiteralPath $selfTestReport

& (Join-Path $binaryDirectory 'store_tests.exe') (Join-Path $testDirectory 'store')
if ($LASTEXITCODE -ne 0) { throw 'Clipboard storage tests failed.' }
& (Join-Path $binaryDirectory 'export_tests.exe') $testDirectory
if ($LASTEXITCODE -ne 0) { throw 'PNG export tests failed.' }
Write-Output 'All native tests passed. Real desktop checks are described in docs/MAINTENANCE.md.'
