# CS2Vault

A native Linux and Windows desktop application for managing your CS2 inventory, storage units, and skin portfolio.

![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-blue)
![Qt](https://img.shields.io/badge/Qt-6.x-green)
![License](https://img.shields.io/badge/license-MIT-orange)
![Build](https://github.com/valletrg/CS2Vault/actions/workflows/build.yml/badge.svg)

## Features

- **Steam Login** — Sign in via QR code or browser token. Login is saved securely so you only authenticate once.
- **Storage Unit Management** — View and manage your CS2 storage unit contents. Move items in and out of storage units directly from the app, with item count and total value displayed per unit.
- **Inventory Browser** — Browse your full CS2 inventory alongside your storage unit contents with search and multi-select.
- **Portfolio Tracking** — Track your CS2 skin investments with buy price, current market value, profit/loss, ROI and value history chart. Items are color coded by rarity.
- **Live Prices** — Prices are fetched from real CS2 marketplaces every 6 hours and served for free. Switch between Skinport, white.market, and market.csgo.com in Settings.
- **Item Database** — Rarity colors, float ranges, and item metadata fetched automatically at startup.
- **Modern UI** — Dark sidebar interface with DM Sans typography and Zen Dots branding.
- **Auto Update Checker** — CS2Vault notifies you when a new version is available.

---

## How Pricing Works

CS2Vault does not scrape Steam or any marketplace directly. Instead, a scheduled GitHub Actions workflow fetches bulk price data from three sources every 6 hours and publishes it to a static hosting endpoint. The app downloads this file once at startup and caches it locally, no API keys, no accounts, no cost to you.

**Price sources:**

| Source | Update Frequency |
| Skinport | Every 6 hours |
| white.market | Every 6 hours |
| market.csgo.com | Every 6 hours |

You can switch between sources in the Settings tab. Prices are cached locally and only re-fetched when the cache expires, keeping the app fast for returning users. This model is what keeps CS2Vault completely free, no subscriptions, no API keys, no paywalls.

---

## Safety FAQ

**Is this safe to use with my Steam account?**
Yes. CS2Vault never asks for your Steam password. Login uses Steam's official QR code flow or a browser refresh token, the same methods used by Steam's own mobile app and other trusted tools. Your credentials never leave Steam's own servers.

**Does this modify game files or interact with CS2 directly?**
No game files are modified. CS2Vault communicates with Steam's Game Coordinator (the same server CS2 itself uses) to read inventory and storage unit data. This is the same approach used by tools like SkinLedger.

**Could my account get banned for using this?**
CS2Vault only reads inventory data and moves items between your inventory and your own storage units, actions that are fully supported by Steam's API. It does not automate gameplay, inject into the game process, or interact with VAC-protected systems in any way.

**What data does CS2Vault store locally?**
CS2Vault stores your portfolio data (`portfolios.json`), a cached copy of price and item data, and your encrypted Steam refresh token, all in your system's standard app data directory. Nothing is sent to any external server except the Steam login flow and the price/item data fetch from the hosting endpoint.

**Is the source code available?**
Yes, CS2Vault is fully open source under the MIT license. You can review every line of code on GitHub.

**The app wants to connect to the internet — why?**
CS2Vault makes three types of network requests: Steam login (via Steam's own servers), price and item data (from the hosting endpoint), and a version check (a tiny JSON file). No analytics, no telemetry, no ads.

---

## Requirements

**Linux:**
- Qt 6.x with Charts and SVG modules
- Node.js 18+

**Windows:**
- Node.js 18+ is bundled in the release — no separate install needed
- Qt6 DLLs are bundled in the release zip

---

## Installing a Release

**Linux:**
1. Download `CS2Vault` from the [latest release](https://github.com/valletrg/CS2Vault/releases/latest)
2. Make it executable: `chmod +x CS2Vault`
3. Install Node.js if needed: `sudo pacman -S nodejs` or `sudo apt install nodejs`
4. Run: `./CS2Vault`

**Windows:**
1. Download and extract `CS2Vault-windows.zip` from the [latest release](https://github.com/valletrg/CS2Vault/releases/latest)
2. Run `CS2Vault.exe` — everything is bundled, no extra installs needed

---

## Building from Source

### Dependencies

```bash
# Arch Linux
sudo pacman -S qt6-base qt6-charts qt6-svg nodejs npm cmake base-devel

# Ubuntu/Debian
sudo apt install qt6-base-dev qt6-charts-dev qt6-svg-dev nodejs npm cmake build-essential
```

### Build

```bash
git clone https://github.com/valletrg/CS2Vault.git
cd CS2Vault
cd steamcompanion && npm install && cd ..
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### Run

```bash
./build/bin/CS2Vault
```

---

## First Launch

On first launch you will see the sign-in screen. You can either:

- **QR Code** — Open the Steam app on your phone, tap the menu and choose "Sign in via QR code", then scan the code shown.
- **Browser Token** — If you are already logged into Steam in your browser, click "Sign in via Browser Token", then visit `steamcommunity.com/chat/clientjstoken` and paste the token value shown.

Your login is saved locally in an encrypted file. Subsequent launches connect automatically.

---

## Contributing

Pull requests are welcome. For major changes please open an issue first to discuss what you would like to change.

---

## License

MIT
