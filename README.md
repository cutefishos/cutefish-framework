# Cutefish Framework

Cutefish Framework provides shared system APIs and backend integrations for
Cutefish desktop components and applications.

It provides common access to application metadata, networking, Bluetooth,
audio, display, appearance, accounts, media controls, and other desktop
services.

## Components

The framework provides these CMake targets and C++ aliases:

- `cutefish-framework-applications` / `Cutefish::Applications`
- `cutefish-framework-accounts` / `Cutefish::Accounts`
- `cutefish-framework-audio` / `Cutefish::Audio`
- `cutefish-framework-appearance` / `Cutefish::Appearance`
- `cutefish-framework-bluetooth` / `Cutefish::Bluetooth`
- `cutefish-framework-media` / `Cutefish::Media`
- `cutefish-framework-network` / `Cutefish::Network`
- `cutefish-framework-screen` / `Cutefish::Screen`

The QML modules are `Cutefish.Accounts`, `Cutefish.Appearance`,
`Cutefish.Audio`, `Cutefish.Bluetooth`, `Cutefish.Media`,
`Cutefish.Network`, and `Cutefish.Screen`.

## Dependencies

The framework uses Qt 6, KDE Frameworks 6, and a C++20 compiler.

### Debian/Ubuntu

```shell
sudo apt install cmake extra-cmake-modules pkg-config build-essential \
    qt6-base-dev qt6-declarative-dev qt6-tools-dev qt6-tools-dev-tools \
    libkscreen-dev libkf6networkmanagerqt-dev libkf6bluezqt-dev \
    libkf6modemmanagerqt-dev libcanberra-dev libpulse-dev \
    sound-theme-freedesktop
```

### Arch Linux

```shell
sudo pacman -S --needed base-devel cmake extra-cmake-modules pkgconf \
    qt6-base qt6-declarative kscreen bluez-qt networkmanager-qt \
    modemmanager-qt libcanberra libpulse sound-theme-freedesktop
```

## Build and Install

```
mkdir build
cd build
cmake ..
make
sudo make install
```

## License

This project has been licensed by GPLv3.
