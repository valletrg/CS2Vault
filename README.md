# CS2Trader

A native Linux desktop application for tracking and managing your CS2 skin portfolio, with real-time Steam Market price checking and Steam inventory integration.

![Platform](https://img.shields.io/badge/platform-Linux-blue)
![Qt](https://img.shields.io/badge/Qt-6.x-green)
![License](https://img.shields.io/badge/license-MIT-orange)
![Build](https://github.com/valletrg/CS2Trader-for-linux/actions/workflows/build.yml/badge.svg)

## Features

- **Portfolio Tracking** — Track your CS2 skin investments with buy price, current value, profit/loss and ROI
- **Real-time Prices** — Fetches live prices directly from the Steam Community Market
- **Steam Inventory Import** — Import your CS2 inventory directly via Steam QR login
- **Storage Unit Support** — View and import items from your CS2 storage units via the Game Coordinator
- **Trade-Up Calculator** — Calculate trade-up contract outcomes including output float range and profit
- **Price Checker** — Look up and compare skin prices across all wear conditions
- **Portfolio Chart** — Visualize your portfolio value over time
- **Price Check Queue** — Background price checking with rate limit handling and pause/resume

## Screenshots

*Coming soon*

## Requirements

- Linux (x86_64)
- Qt 6.x with Charts module
- Node.js 18+ (for Steam inventory integration)

## Building from Source

### Dependencies
```bash
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

# Build the Qt app
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### Run
```bash
./build/bin/CS2Trader
```

## Steam Integration

CS2Trader uses a Node.js companion process to communicate with Steam. On first launch, click **Sign in with Steam QR** in the Portfolio tab and scan the QR code with your Steam mobile app. Your login is saved locally — you only need to scan once.

Storage unit contents are fetched directly from the CS2 Game Coordinator, the same way tools like SkinLedger work.

## Price Data

Prices are fetched from the Steam Community Market and are subject to Steam's rate limits. When importing an inventory, prices are checked in the background at ~1 per 1.5 seconds to avoid being rate limited. You can pause and resume the price check queue at any time.

## Project Structure
```
CS2Trader-for-linux/
├── steamcompanion/       # Node.js Steam companion process
│   └── index.js          # Steam login, inventory & GC communication
├── *.cpp / *.h           # Qt application source
├── CMakeLists.txt        # Build configuration
└── .github/workflows/    # CI/CD for Linux and Windows builds
```

## Contributing

Pull requests are welcome. For major changes please open an issue first.

## License

MIT
