# CS2Vault

A native Linux and Windows desktop app for managing your CS2 inventory, storage units, and skin portfolio. Built with Qt6 and a Node.js companion that connects directly to Steam's Game Coordinator.

![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-blue)
![Qt](https://img.shields.io/badge/Qt-6.x-green)
![License](https://img.shields.io/badge/license-MIT-orange)
![Build](https://github.com/valletrg/CS2Vault/actions/workflows/build.yml/badge.svg)

---

## What it does

**Home Dashboard** - See your total portfolio value, recent price changes, and a quick overview of your most valuable items at a glance.

**Storage Unit Management** - Browse and manage your CS2 storage units directly from the app. Move items in and out, see item counts, and get total value estimates per unit.

**Portfolio Tracking** - Track your skin investments with buy price, current market value, profit/loss, ROI, and a full price history chart. Items are color coded by rarity.

**Price History Chart** - A proper financial-style chart showing portfolio value over time with time range filters (24H, 7D, 1M, 3M, 6M, 1Y), performance stats, and a buy-in cost reference line.

**Watchlist** - Keep an eye on skins you want to buy without adding them to your portfolio. Tracks current price and price changes across your watched items.

**Live Prices** - Prices come from real CS2 marketplaces, updated every 6 hours and cached locally. Switch between Skinport, white.market, and market.csgo.com in Settings.

**Multiple Steam Accounts** - Add and switch between multiple Steam accounts. Each account has its own encrypted login token stored locally.

**Item Database** - Rarity colors, float ranges, and item metadata loaded at startup from a hosted database so nothing bulky is bundled in the app itself.

**Auto Update Checker** - CS2Vault tells you when a new version is available.

---

## How pricing works

CS2Vault never scrapes Steam directly. A scheduled GitHub Actions workflow fetches bulk price data from three marketplaces every 6 hours and publishes it to a static endpoint. The app downloads the file once at startup and caches it locally so subsequent launches are instant.

| Source | Update Frequency |
|--------|-----------------|
| Skinport | Every 6 hours |
| white.market | Every 6 hours |
| market.csgo.com | Every 6 hours |

You can switch sources in Settings. This approach is what keeps CS2Vault completely free with no API keys or subscriptions needed.

---

## Safety FAQ

**Is this safe to use with my Steam account?**
Yes. CS2Vault never asks for your password. Login uses Steam's official QR code flow or a browser token, the same methods Steam's own mobile app uses. Your credentials never leave Steam's servers.

**Does this modify game files or interact with CS2 directly?**
No game files are modified. CS2Vault talks to Steam's Game Coordinator to read your inventory and storage unit data. The same approach is used by tools like SkinLedger.

**Could my account get banned?**
CS2Vault only reads inventory data and moves items between your own inventory and storage units, which are fully supported Steam API operations. It does not automate gameplay, inject into the game process, or touch VAC-protected systems.

**What data does CS2Vault store locally?**
Your portfolio data, a cached copy of price and item data, and your encrypted Steam refresh tokens, all stored in your system's standard app data directory. Nothing gets sent to any external server except the Steam login flow and the price and item data fetch from the hosted endpoint.

**Is the browser token login safe?**
Yes. The token from `steamcommunity.com/chat/clientjstoken` is a one-time web token that expires shortly after use. It is not your password and cannot be reused once consumed. Steam generates a fresh one each time you visit that page.

**Is the source code available?**
Yes, CS2Vault is fully open source under MIT. Every line of code is on GitHub.

**Why does it need internet access?**
Three types of requests happen: Steam login via Steam's own servers, price and item data from the hosted endpoint, and a small version check. No analytics, no telemetry, no ads.

---

## Requirements

**Linux:**
- Qt 6.x with Charts and SVG modules
- Node.js 18+

**Windows:**
- Node.js 18+ is bundled in the release
- Qt6 DLLs are bundled in the release zip

---

## Installing a release

**Linux:**
1. Download `CS2Vault` from the [latest release](https://github.com/valletrg/CS2Vault/releases/latest)
2. Make it executable: `chmod +x CS2Vault`
3. Install Node.js if needed: `sudo pacman -S nodejs` or `sudo apt install nodejs`
4. Run: `./CS2Vault`

**Windows:**
1. Download and extract `CS2Vault-windows.zip` from the [latest release](https://github.com/valletrg/CS2Vault/releases/latest)
2. Run `CS2Vault.exe`, everything is bundled

---

## Building from source

**Dependencies:**

```bash
# Arch Linux
sudo pacman -S qt6-base qt6-charts qt6-svg nodejs npm cmake base-devel

# Ubuntu/Debian
sudo apt install qt6-base-dev qt6-charts-dev qt6-svg-dev nodejs npm cmake build-essential
```

**Build:**

```bash
git clone https://github.com/valletrg/CS2Vault.git
cd CS2Vault
cd steamcompanion && npm install && cd ..
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

**Run:**

```bash
./build/bin/CS2Vault
```

---

## First launch

On first launch you will see the sign-in screen. Two options:

**QR Code** - Open the Steam app on your phone, tap the menu and choose "Sign in via QR code", then scan the code shown.

**Browser Token** - If you are already logged into Steam in your browser, click "Sign in via Browser Token", visit `steamcommunity.com/chat/clientjstoken`, select all the text on that page with Ctrl+A, and paste it into CS2Vault.

Your login is saved locally in an encrypted file so subsequent launches connect automatically.

---

## Support CS2Vault

If you find CS2Vault useful, contributions are always appreciated!

🟠 **Bitcoin (BTC)**
`bc1q6586wt0ttsarrlepwp0kp7ve78f4ua22sry0n9`

🔵 **Litecoin (LTC)**
`ltc1qfwks2uzeh2lwrrz7kpymguqlmwj0thv9226tgy`

🟣 **Ethereum (ETH)**
`0xBA74E8cE7D6062066b6D43113C73C36E23F4e6D6`

🟢 **Solana (SOL)**
`HUmRbrVBjDz137dB6Sfzytsd64fuKUBgWC3UDmLPEhWi`

---


## Support CS2Vault

If you find CS2Vault useful, contributions are always appreciated!

🟠 **Bitcoin (BTC)**
`bc1q6586wt0ttsarrlepwp0kp7ve78f4ua22sry0n9`

🔵 **Litecoin (LTC)**
`ltc1qfwks2uzeh2lwrrz7kpymguqlmwj0thv9226tgy`

🟣 **Ethereum (ETH)**
`0xBA74E8cE7D6062066b6D43113C73C36E23F4e6D6`

🟢 **Solana (SOL)**
`HUmRbrVBjDz137dB6Sfzytsd64fuKUBgWC3UDmLPEhWi`

---

## Contributing

Pull requests are welcome. For bigger changes open an issue first so we can discuss the approach.

---

## License

MIT