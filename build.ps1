$ErrorActionPreference = "Stop"

$env:CHERE_INVOKING = "1"
$env:MSYSTEM = "UCRT64"
$env:PATH = "C:\work\version\GitHub\yoMMD\msys64\ucrt64\bin;C:\work\version\GitHub\yoMMD\msys64\usr\bin;" + ";" + $env:PATH

bash -lc "make build-submodule && make release -j4"
