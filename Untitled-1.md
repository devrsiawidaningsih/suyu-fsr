# Tutorial build Suyu APK di Windows 11 menggunakan WSL2

Konfigurasi disesuaikan dengan RAM 16 GBDengan komputer Windows 11, RAM 16 GB, dan Intel Core i5, kamu bisa melakukan build Suyu secara lokal tanpa menyewa server.

Saya menyarankan konfigurasi berikut:

| Komponen | Konfigurasi |
| --- | --- |
| OS utama | Windows 11 64-bit |
| Linux | Ubuntu 24.04 LTS (WSL2) |
| RAM WSL | Maksimal 10 GB |
| CPU WSL | 4 core (jika tersedia) |
| Java | JDK 17 |
| Android SDK | Platform 36 |
| Android NDK | 28.2.13676358 |
| Android CMake | 3.31.6 |
| Target APK | Android ARM64 |
| Build | Release |

Panduan ini mengacu pada instruksi build repository Suyu dan konfigurasi Android CI yang tercantum di README repository.

GitHub

+1

## 1. Install WSL2 Ubuntu

Buka Windows PowerShell dengan Run as Administrator.

Jalankan:

powershell

```powershell
wsl --install -d Ubuntu-24.04
```

Tunggu proses instalasi selesai, kemudian restart komputer jika diminta.

Buka Ubuntu 24.04 dari Start Menu.

Buat username dan password Linux.

Periksa versi WSL:

powershell

```powershell
wsl -l -v
```

Pastikan Ubuntu menggunakan WSL versi 2.

## 2\. Atur penggunaan RAM WSL

Karena RAM komputer kamu 16 GB, kita batasi penggunaan WSL agar Windows tetap memiliki ruang memori.

Di Windows, buka Notepad dan masukkan:

ini

```ini
[wsl2]
memory=10GB
processors=4
swap=8GB
localhostForwarding=true
```

Simpan sebagai:

text

```text
.wslconfig
```

Lokasi penyimpanan:

text

```text
C:\Users\USERNAME\.wslconfig
```

Ganti `USERNAME` dengan nama akun Windows kamu.

Kemudian di PowerShell:

powershell

```powershell
wsl --shutdown
```

Buka kembali Ubuntu.

## 3\. Install dependency Ubuntu

Di terminal Ubuntu jalankan:

bash

```bash
sudo apt update
sudo apt upgrade -y
```

Install kebutuhan build:

bash

```bash
sudo apt install -y \
git curl wget unzip zip \
build-essential pkg-config \
ninja-build nasm autoconf \
glslang-tools \
openjdk-17-jdk \
ca-certificates
```

Periksa Java:

bash

```bash
java -version
```

Seharusnya menunjukkan Java 17.

## 4\. Install Android SDK khusus Linux WSL2

Penting: Saya menyarankan Android SDK dipasang di dalam Ubuntu WSL2, bukan menggunakan SDK Windows di `C:\Users\...`. Alasannya, build di WSL memerlukan executable SDK dan NDK untuk Linux.

Buat direktori:

bash

```bash
mkdir -p ~/Android/Sdk/cmdline-tools
cd ~/Android/Sdk
```

Unduh Android Command Line Tools Linux dari halaman resmi:

[Android Studio – Command Line Tools](<https://developer.android.com/studio?utm_source=chatgpt.com#command-line-tools-only>)

Pilih paket Linux, lalu simpan ZIP di Ubuntu.

Ekstrak dengan:

bash

```bash
unzip commandlinetools-linux-*.zip
```

Atur direktori menjadi:

bash

```bash
mkdir -p ~/Android/Sdk/cmdline-tools/latest

mv cmdline-tools/* \
~/Android/Sdk/cmdline-tools/latest/
```

Atur environment variable:

bash

```bash
echo 'export ANDROID_HOME=$HOME/Android/Sdk' >> ~/.bashrc

echo 'export ANDROID_SDK_ROOT=$ANDROID_HOME' >> ~/.bashrc

echo 'export PATH=$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH' >> ~/.bashrc

source ~/.bashrc
```

Periksa:

bash

```bash
sdkmanager --version
```

## 5\. Install SDK, NDK dan CMake

Jalankan perintah berikut:

bash

```bash
sdkmanager --licenses
```

Terima seluruh lisensi yang diminta.

Kemudian install versi yang sesuai dengan konfigurasi CI Suyu:

bash

```bash
sdkmanager \
--sdk_root="$ANDROID_HOME" \
"platform-tools" \
"platforms;android-36" \
"ndk;28.2.13676358" \
"cmake;3.31.6"
```

Verifikasi instalasi:

bash

```bash
ls "$ANDROID_HOME/ndk"
ls "$ANDROID_HOME/cmake"
```

Seharusnya tersedia:

text

```text
28.2.13676358
3.31.6
```

## 6\. Download source code Suyu

Saya menyarankan menggunakan source rilis `v14.1.0` terlebih dahulu supaya build lebih konsisten dibandingkan mengambil perubahan development terbaru.

Pastikan kamu berada di filesystem Linux, bukan `/mnt/c`.

Jalankan:

bash

```bash
cd ~
```

Clone repository:

bash

```bash
git clone --recursive \
--branch v14.1.0 \
https://github.com/suyu-emu/suyu-main.git suyu
```

Masuk ke folder:

bash

```bash
cd ~/suyu
```

Pastikan submodule sudah lengkap:

bash

```bash
git submodule update --init --recursive
```

Periksa branch dan versi:

bash

```bash
git describe --tags --always
```

Repository resmi: [suyu-main](<https://github.com/suyu-emu/suyu-main?utm_source=chatgpt.com>).

## 7\. Persiapkan dependency build Android

Jalankan:

bash

```bash
sudo apt install -y \
ninja-build \
glslang-tools \
nasm
```

Pastikan environment Android terbaca:

bash

```bash
echo $ANDROID_HOME
echo $ANDROID_NDK_HOME
```

Jika ingin menetapkan lokasi NDK secara eksplisit:

bash

```bash
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/28.2.13676358"
```

Tambahkan agar permanen:

bash

```bash
echo 'export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/28.2.13676358"' >> ~/.bashrc
```

## 8\. Mulai build Suyu APK

Tahap utamaMasuk ke direktori repository:

bash

```bash
cd ~/suyu
```

Periksa script build:

bash

```bash
ls .ci/android/build.sh
```

Jika file ditemukan, jalankan:

bash

```bash
sh .ci/android/build.sh -t standard -b Release
```

Proses ini akan melakukan:

- Konfigurasi compiler Android.
- Kompilasi source code C++.
- Build komponen emulator.
- Packaging aplikasi Android.
- Pembuatan APK release.

Rekomendasi untuk komputer RAM 16 GB:

- Tutup aplikasi berat seperti game dan browser dengan banyak tab.
- Jangan menjalankan build lain bersamaan.
- Pastikan drive Windows memiliki ruang kosong yang cukup.
- Jangan mematikan terminal selama proses build.

Waktu build tidak bisa dipastikan karena bergantung pada generasi Intel Core i5, jumlah core/thread, kecepatan SSD, dan dependency yang perlu dikompilasi.

## 9\. Ambil hasil APK

Jika build berhasil, menurut README repository, hasil APK berada di:

text

```text
~/suyu/artifacts/app-mainline-release.apk
```

Periksa:

bash

```bash
ls -lh ~/suyu/artifacts/
```

Buka folder tersebut dari Windows File Explorer:

text

```text
\\wsl.localhost\Ubuntu-24.04\home\USERNAME\suyu\artifacts
```

Ganti `USERNAME` dengan username Ubuntu kamu.

Kamu bisa menyalin APK ke desktop Windows atau langsung ke HP Android.

## 10\. Install APK ke HP Android

Aktifkan USB debugging di HP Android dan sambungkan ke komputer.

Di Ubuntu:

bash

```bash
adb devices
```

Jika perangkat terdeteksi:

bash

```bash
adb install -r \
~/suyu/artifacts/app-mainline-release.apk
```

Jika instalasi gagal karena konflik signature dengan APK Suyu yang sudah terpasang, kamu mungkin perlu menghapus aplikasi lama terlebih dahulu.

## 11\. Jika build mengalami error

Berikut beberapa error yang mungkin muncul:

| Error | Solusi |
| --- | --- |
| `sdkmanager: command not found` | Periksa PATH Android SDK |
| `JAVA_HOME is not set` | Periksa instalasi JDK 17 |
| `CMake 3.31 required` | Pastikan CMake SDK 3.31.6 terinstal |
| `NDK not found` | Periksa lokasi NDK |
| `No space left on device` | Kosongkan ruang SSD |
| Build berhenti karena RAM | Kurangi proses lain dan periksa swap WSL |

Koreksi kecil untuk langkah ekstraksi Command Line Tools: agar tidak terjadi pemindahan folder ke dalam dirinya sendiri, gunakan direktori sementara seperti berikut:

bash

```bash
mkdir -p ~/android-tools-temp
cd ~/android-tools-temp

unzip ~/commandlinetools-linux-*.zip

mkdir -p ~/Android/Sdk/cmdline-tools/latest

cp -a cmdline-tools/. \
~/Android/Sdk/cmdline-tools/latest/
```

Sesuaikan lokasi ZIP jika kamu menyimpannya di folder lain.

## 12\. Gambaran alur build

Berikut alur keseluruhan yang akan kamu lakukan:

Windows 11

RAM 16 GB · Intel Core i5

WSL2 Ubuntu 24.04

Linux build environment

Android SDK + NDK

Java 17 · NDK 28.2 · CMake 3.31.6

Suyu Source Code

Git + recursive submodules

app-mainline-release.apk

APK Android ARM64

## Rekomendasi saya

Untuk spesifikasi kamu, saya menyarankan build pertama menggunakan konfigurasi standar `Release` terlebih dahulu. Jangan mengubah optimasi compiler atau source code sebelum berhasil menghasilkan APK pertama.

Setelah berhasil, barulah kita bisa membuat versi custom dengan nama aplikasi, ikon, splash screen, dan identitas package sendiri.

Ada dua hal yang bisa saya bantu lanjutkan:

- Buatkan script otomatis build-suyu.sh
- Buatkan versi Suyu custom dengan ikon sendiri