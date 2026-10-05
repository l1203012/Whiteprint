# Puts the user-level Qt 6.8.3 (MinGW), CMake and Ninja from C:\Qt on PATH for this shell.
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.8.3\mingw_64\bin;$env:PATH"
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.3\mingw_64"
$env:QT_QPA_PLATFORM = if ($env:QT_QPA_PLATFORM) { $env:QT_QPA_PLATFORM } else { "offscreen" }
$env:QT_QPA_FONTDIR = if ($env:QT_QPA_FONTDIR) { $env:QT_QPA_FONTDIR } else { "C:/Windows/Fonts" }
