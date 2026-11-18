param(
    [string]$ArduinoCli = 'arduino-cli',
    [string]$ConfigFile,
    [string]$UploadPort
)
$ErrorActionPreference = 'Stop'
$revision = 'd5ed3b777bedb9b407ccff09dc810a9248eb7926'
$raw = "https://raw.githubusercontent.com/gooddisplayshare/ESP32epdx/$revision"
$work = Join-Path $PSScriptRoot '.build'
$cache = Join-Path $work $revision
$sketch = Join-Path $work 'ControlRoom'
$lib = Join-Path $work 'libraries/ESP32epdx'
$panel = 'examples/3-Colors (BWR)/7.5/GDEY075Z08'
$null = New-Item -ItemType Directory -Force $cache,$sketch,$lib

function Copy-Upstream([string]$Path, [string]$Destination) {
    $saved = Join-Path $cache $Path
    $null = New-Item -ItemType Directory -Force (Split-Path $saved),(Split-Path $Destination)
    if (!(Test-Path -LiteralPath $saved)) {
        $url = $raw + '/' + (($Path.Split('/') | ForEach-Object {[uri]::EscapeDataString($_)}) -join '/')
        $tempFile = "$saved.part"
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $tempFile
        Move-Item -Force -LiteralPath $tempFile -Destination $saved
    }
    Copy-Item -Force -LiteralPath $saved -Destination $Destination
}

$libraryFiles = @('library.properties', 'src/ESP32epdx.h', 'src/ESP32epdx.cpp',
    'src/EPD_SPI.h', 'src/EPD_SPI.cpp', 'src/GUI/GUI_Paint.h', 'src/GUI/GUI_Paint.cpp',
    'src/GUI/fonts.h', 'src/GUI/font8.cpp', 'src/GUI/font12.cpp', 'src/GUI/font16.cpp',
    'src/GUI/font20.cpp', 'src/GUI/font24.cpp')
foreach ($file in $libraryFiles) { Copy-Upstream $file (Join-Path $lib $file) }
foreach ($file in @('EPD.cpp','EPD.h')) { Copy-Upstream "$panel/$file" (Join-Path $sketch $file) }

# Apply only the documented integration fixes to this specific upstream driver.
# Fail if it has unexpected contents (including an incomplete earlier download).
$driverFile = Join-Path $sketch 'EPD.cpp'
$driver = [IO.File]::ReadAllText($driverFile)
if ($driver -notmatch 'unsigned char PartImage\[1000\];' -or
    $driver -notmatch 'void lcd_chkstatus\(void\)' -or
    $driver -notmatch 'x_end%256-1' -or $driver -notmatch 'y_end%256-1') {
    throw 'Unexpected Good Display driver. Remove the cached EPD.cpp and rebuild.'
}
$driver = $driver.Replace('unsigned char PartImage[1000];', 'extern unsigned char *PartImage;')
$start = $driver.IndexOf('void lcd_chkstatus(void)')
$end = $driver.IndexOf('//Full screen update initialization', $start)
if ($end -lt 0) { throw 'Unsupported BUSY function boundary' }
$driver = $driver.Substring(0,$start) + "extern bool panelReady();`n" + $driver.Substring($end)
$driver = $driver.Replace('lcd_chkstatus();','if (!panelReady()) return;')
# The example already calculates the inclusive last pixel as start + size - 1.
$driver = $driver.Replace('x_end%256-1','x_end%256').Replace('y_end%256-1','y_end%256')
[IO.File]::WriteAllText($driverFile,$driver,[Text.UTF8Encoding]::new($false))

Copy-Item -Force (Join-Path $PSScriptRoot 'ControlRoom/*') $sketch
Copy-Item -Force -LiteralPath (Join-Path $PSScriptRoot '../stm32/site_profile.h') -Destination $sketch
$options = @()
if ($ConfigFile) { $options += @('--config-file',$ConfigFile) }
$fqbn = 'esp32:esp32:esp32'
$bin = Join-Path $work 'bin'
& $ArduinoCli compile @options --fqbn $fqbn --libraries (Join-Path $work 'libraries') --output-dir $bin $sketch
if ($LASTEXITCODE -ne 0) { throw "Arduino compile failed ($LASTEXITCODE)" }
if ($UploadPort) {
    & $ArduinoCli upload @options --fqbn $fqbn --port $UploadPort --input-dir $bin $sketch
    if ($LASTEXITCODE -ne 0) { throw "Arduino upload failed ($LASTEXITCODE)" }
}
