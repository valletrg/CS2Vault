# CS2Vault

A native Linux desktop application for managing your CS2 inventory, storage units, and skin portfolio.

![Platform](https://img.shields.io/badge/platform-Linux-blue)
![Qt](https://img.shields.io/badge/Qt-6.x-green)
![License](https://img.shields.io/badge/license-MIT-orange)
![Build](https://github.com/valletrg/CS2Trader-for-linux/actions/workflows/build.yml/badge.svg)

## Features

- **Steam Login** — Sign in via QR code or browser token. Login is saved securely so you only authenticate once.
- **Storage Unit Management** — View and manage your CS2 storage unit contents. Move items in and out of storage units directly from the app, the same way SkinLedger does it.
- **Inventory Browser** — Browse your full CS2 inventory alongside your storage unit contents with search and multi-select.
- **Portfolio Tracking** — Track your CS2 skin investments with buy price, current market value, profit/loss, ROI and value history chart.
- **Steam Market Prices** — Fetches live prices directly from the Steam Community Market with rate-limit handling and background price check queue.
- **Trade-Up Calculator** — Calculate trade-up contract output float range and profit/loss.
- **Price Checker** — Look up and compare skin prices across all wear conditions.

## How it works

CS2Vault uses a Node.js companion process (`steamcompanion/`) to communicate with Steam's servers. The companion connects to the CS2 Game Coordinator — the same approach used by tools like SkinLedger — which is what enables storage unit access. Your login token is encrypted and stored locally so you only need to sign in once per machine.

## Screenshots

*Coming soon*

## Requirements

- Linux (x86_64)
- Qt 6.x with Charts module
- Node.js 18+

## Building from Source

### Dependencies

```bash
# Arch Linux
sudo pacman -S qt6-base qt6-charts nodejs npm cmake base-devel

# Ubuntu/Debian
sudo apt install qt6-base-dev qt6-charts-dev nodejs npm cmake build-essential
```

### Build

```bash
git clone https://github.com/valletrg/CS2Trader-for-linux.git
cd CS2Trader-for-linux

# Set up the Steam companion
cd steamcompanion
npm install
cd ..

# Download item databases (needed for storage unit item names)
cd steamcompanion
curl -o skins.json "https://raw.githubusercontent.com/ByMykel/CSGO-API/main/public/api/en/skins_not_grouped.json"
curl -o crates.json "https://raw.githubusercontent.com/ByMykel/CSGO-API/main/public/api/en/crates.json"
curl -o graffiti.json "https://raw.githubusercontent.com/ByMykel/CSGO-API/main/public/api/en/graffiti.json"
curl -o agents.json "https://raw.githubusercontent.com/ByMykel/CSGO-API/main/public/api/en/agents.json"
curl -o patches.json "https://raw.githubusercontent.com/ByMykel/CSGO-API/main/public/api/en/patches.json"
curl -o collectibles.json "https://raw.githubusercontent.com/ByMykel/CSGO-API/main/public/api/en/collectibles.json"
cd ..

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### Run

```bash
./build/bin/CS2Vault
```

## First Launch

On first launch you will see the sign-in screen. You can either:

- **QR Code** — Open the Steam app on your phone, tap the menu and choose "Sign in via QR code", then scan the code shown.
- **Browser Token** — If you are already logged into Steam in your browser, click "Sign in via Browser Token" and follow the instructions to get a one-time token.

Your login is saved locally in an encrypted file. Subsequent launches will connect automatically.


## Contributing

Pull requests are welcome. For major changes please open an issue first to discuss what you would like to change.

## License

MIT