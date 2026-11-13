@echo off
cmake -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DENABLE_QT=ON -DYUZU_USE_BUNDLED_QT=OFF ^
  -DYUZU_TESTS=OFF -DENABLE_WEB_SERVICE=OFF ^
  -DSUYU_EXPORT_BUILD_KIT=ON ^
  -DCMAKE_PREFIX_PATH="C:/Users/Monster-PC/6.9.3/msvc2022_64"

if %ERRORLEVEL% EQU 0 (
    cmake --build build --target suyu suyu-cmd export-build-kit
) else (
    echo Konfigurasi CMake Gagal! Proses build dibatalkan.
)
pause