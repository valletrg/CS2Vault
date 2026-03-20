const SteamUser = require('steam-user');
const GlobalOffensive = require('node-cs2');
const { LoginSession, EAuthTokenPlatformType } = require('steam-session');
const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const os = require('os');
const https = require('https');
const ITEMS_CACHE_MAX_AGE_MS = 24 * 60 * 60 * 1000; // 24 hours

if (process.pkg) {
    process.chdir(path.dirname(process.execPath));
} else {
    process.chdir(__dirname);
}

process.stderr.write(`Working directory: ${process.cwd()}\n`);

const SteamCommunity = require('steamcommunity');
const community = new SteamCommunity();
const client = new SteamUser();
const csgo = new GlobalOffensive(client);

const DB_DIR = process.pkg
    ? path.dirname(process.execPath)
    : __dirname;

const REFRESH_TOKEN_FILE = path.join(DB_DIR, 'refresh_token.txt');

// Derive a machine-specific key from hostname + username
const MACHINE_KEY = crypto.createHash('sha256')
    .update(os.hostname() + os.userInfo().username + 'cs2trader-salt')
    .digest();

function encryptToken(plaintext) {
    const iv = crypto.randomBytes(16);
    const cipher = crypto.createCipheriv('aes-256-cbc', MACHINE_KEY, iv);
    const encrypted = Buffer.concat([cipher.update(plaintext, 'utf8'), cipher.final()]);
    return iv.toString('hex') + ':' + encrypted.toString('hex');
}

function decryptToken(ciphertext) {
    const [ivHex, encHex] = ciphertext.split(':');
    const iv = Buffer.from(ivHex, 'hex');
    const encrypted = Buffer.from(encHex, 'hex');
    const decipher = crypto.createDecipheriv('aes-256-cbc', MACHINE_KEY, iv);
    return Buffer.concat([decipher.update(encrypted), decipher.final()]).toString('utf8');
}

function send(type, data) {
    process.stdout.write(JSON.stringify({ type, ...data }) + '\n');
}

function sendError(message) {
    send('error', { message });
}

// ── Item database ─────────────────────────────────────────────────────────────

let skinsByKey = {};
let defIndexDb = {};
let dbReady = false;

function fetchUrl(url, redirectCount = 0) {
    return new Promise((resolve, reject) => {
        if (redirectCount > 5) return reject(new Error('Too many redirects'));

        const request = https.get(url, {
            headers: { 'User-Agent': 'CS2Vault/1.0.0' },
            timeout: 15000
        }, (res) => {
            if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
                return fetchUrl(res.headers.location, redirectCount + 1)
                    .then(resolve).catch(reject);
            }
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => resolve({ status: res.statusCode, body: data }));
            res.on('error', reject);
        });

        request.on('timeout', () => {
            request.destroy();
            reject(new Error('Request timed out'));
        });

        request.on('error', reject);
    });
}

function fetchItemDatabase() {
    process.stderr.write('Fetching item database from fursense.lol...\n');
    const startTime = Date.now();
    return fetchUrl('https://fursense.lol/items-extended.json')
        .then(({ status, body }) => {
            process.stderr.write(`Fetch took ${Date.now() - startTime}ms, size: ${(body.length / 1024 / 1024).toFixed(2)}MB\n`);
            if (status !== 200) {
                throw new Error(`HTTP ${status}`);
            }

            const parsed = JSON.parse(body);
            const items = parsed.items || {};

            for (const [name, entry] of Object.entries(items)) {
                if (entry.t === 'skin') {
                    if (entry.wi && entry.pi) {
                        const key = `${entry.wi}_${entry.pi}`;
                        if (!skinsByKey[key]) {
                            const parts = name.split(' | ');
                            const weaponName = parts[0] || '';
                            const patternWithCondition = parts[1] || '';
                            const patternName = patternWithCondition.replace(/\s*\(.*\)$/, '');
                            skinsByKey[key] = {
                                market_hash_name: name,
                                weapon: { name: weaponName },
                                pattern: { name: patternName }
                            };
                        }
                    }
                } else {
                    if (entry.di) {
                        const key = String(entry.di);
                        if (!defIndexDb[key]) {
                            defIndexDb[key] = {
                                market_hash_name: name,
                                name: name,
                                _type: entry.t
                            };
                        }
                    }
                }
            }

            dbReady = true;
            process.stderr.write(`Item DB loaded: ${Object.keys(skinsByKey).length} skins, ${Object.keys(defIndexDb).length} other items\n`);
        });
}

// ── Item name resolution ──────────────────────────────────────────────────────

function wearFromFloat(f) {
    if (f === null || f === undefined) return '';
    if (f < 0.07) return 'Factory New';
    if (f < 0.15) return 'Minimal Wear';
    if (f < 0.38) return 'Field-Tested';
    if (f < 0.45) return 'Well-Worn';
    return 'Battle-Scarred';
}

function resolveItemName(defIndex, paintIndex, paintWear) {
    if (defIndex === 1209) return 'Sealed Graffiti';

    if (paintIndex && defIndex) {
        const key = `${defIndex}_${paintIndex}`;
        const skin = skinsByKey[key];
        if (skin) {
            const weaponName = skin.weapon?.name || '';
            const patternName = skin.pattern?.name || '';
            const condition = wearFromFloat(paintWear);
            if (weaponName && patternName && condition) {
                return `${weaponName} | ${patternName} (${condition})`;
            }
            return skin.market_hash_name || `Unknown (def:${defIndex})`;
        }
    }

    if (defIndex) {
        const entry = defIndexDb[String(defIndex)];
        if (entry) return entry.market_hash_name || entry.name;
    }

    return `Unknown (def:${defIndex})`;
}

function resolveMarketHashName(defIndex, paintIndex, paintWear) {
    if (paintIndex && defIndex) {
        const key = `${defIndex}_${paintIndex}`;
        const skin = skinsByKey[key];
        if (skin) {
            const weaponName = skin.weapon?.name || '';
            const patternName = skin.pattern?.name || '';
            const condition = wearFromFloat(paintWear);
            if (weaponName && patternName && condition) {
                return `${weaponName} | ${patternName} (${condition})`;
            }
            return skin.market_hash_name || null;
        }
    }
    const entry = defIndexDb[String(defIndex)];
    if (entry) return entry.market_hash_name || entry.name;
    return null;
}

// ── Login ─────────────────────────────────────────────────────────────────────

async function startLogin() {
    send('status', { state: 'starting' });

    if (fs.existsSync(REFRESH_TOKEN_FILE)) {
        try {
            const encrypted = fs.readFileSync(REFRESH_TOKEN_FILE, 'utf8').trim();
            const refreshToken = decryptToken(encrypted);
            send('status', { state: 'using_saved_token' });
            client.logOn({ refreshToken });
            return;
        } catch (e) {
            process.stderr.write('Token decrypt failed, re-authenticating\n');
            fs.unlinkSync(REFRESH_TOKEN_FILE);
        }
    }

    send('status', { state: 'creating_session' });
    const session = new LoginSession(EAuthTokenPlatformType.SteamClient);

    session.on('remoteInteraction', () => {
        send('status', { state: 'qr_scanned', message: 'QR scanned! Approve in Steam app...' });
    });

    session.on('authenticated', async () => {
        const refreshToken = session.refreshToken;
        fs.writeFileSync(REFRESH_TOKEN_FILE, encryptToken(refreshToken));
        send('status', { state: 'authenticated' });
        client.logOn({ refreshToken });
    });

    session.on('timeout', () => {
        sendError('QR code timed out. Restart to try again.');
        process.exit(1);
    });

    session.on('error', (err) => {
        sendError('Session error: ' + err.message);
        process.exit(1);
    });

    send('status', { state: 'starting_qr' });
    const result = await session.startWithQR();
    send('status', { state: 'qr_code', url: result.qrChallengeUrl });

    try {
        execSync("qrencode -t UTF8 '" + result.qrChallengeUrl + "'", { stdio: ['ignore', 'ignore', 'inherit'] });
    } catch (e) {
        // qrencode not available
    }
}

// ── Steam events ──────────────────────────────────────────────────────────────

client.on('loggedOn', () => {
    send('status', { state: 'logged_in', steamid: client.steamID.toString() });
    client.setPersona(SteamUser.EPersonaState.Online);
    client.gamesPlayed([730]);

    client.on('webSession', (sessionId, cookies) => {
        community.setCookies(cookies);
        send('status', { state: 'web_session_ready' });
    });

    setTimeout(() => {
        if (!csgo.haveGCSession) {
            send('status', { state: 'gc_connecting', message: 'Still waiting for GC...' });
        }
    }, 15000);
});

client.on('refreshToken', (token) => {
    fs.writeFileSync(REFRESH_TOKEN_FILE, encryptToken(token));
});

client.on('error', (err) => {
    if (err.eresult === 84 || err.eresult === 65) {
        if (fs.existsSync(REFRESH_TOKEN_FILE)) fs.unlinkSync(REFRESH_TOKEN_FILE);
        send('status', { state: 'token_expired' });
    }
    sendError('Steam error: ' + err.message);
    process.exit(1);
});

client.on('disconnected', (eresult, msg) => {
    send('status', { state: 'disconnected', reason: msg });
});

// ── CS2 Game Coordinator events ───────────────────────────────────────────────

csgo.on('connectedToGC', () => {
    send('status', { state: 'gc_ready' });

    setTimeout(() => {
        const caskets = csgo.inventory.filter(item =>
            item.casket_contained_item_count !== undefined
        );
        send('status', {
            state: 'gc_inventory_loaded',
            total_items: csgo.inventory.length,
            caskets: caskets.map(c => ({
                id: c.id.toString(),
                name: c.custom_name || 'Storage Unit',
                item_count: c.casket_contained_item_count
            }))
        });

        if (caskets.length > 0) {
            send('inventory', {
                items: [],
                containers: caskets.map(c => ({
                    id: c.id.toString(),
                    name: c.custom_name || 'Storage Unit'
                }))
            });
        }
    }, 3000);
});

csgo.on('disconnectedFromGC', (reason) => {
    send('status', { state: 'gc_disconnected', reason });
});

// ── Inventory ─────────────────────────────────────────────────────────────────

function requestInventory() {
    community.getUserInventoryContents(client.steamID, 730, 2, true, (err, items) => {
        if (err) {
            sendError('Inventory error: ' + err.message);
            return;
        }

        const containers = items.filter(item =>
            item.market_hash_name?.includes('Storage Unit')
        );

        const regularItems = items.filter(item =>
            !item.market_hash_name?.includes('Storage Unit')
        );

        const gcCaskets = csgo.inventory
            ? csgo.inventory.filter(item => item.casket_contained_item_count !== undefined)
            : [];

        const allContainers = [
            ...containers.map(c => ({ id: c.assetid, name: c.market_hash_name })),
            ...gcCaskets
                .filter(c => !containers.find(wc => wc.assetid === c.id.toString()))
                .map(c => ({ id: c.id.toString(), name: c.custom_name || 'Storage Unit' }))
        ];

        send('inventory', {
            items: regularItems.map(item => ({
                id: item.assetid,
                name: item.market_hash_name,
                market_hash_name: item.market_hash_name,
                exterior: item.tags?.find(t => t.category === 'Exterior')?.localized_tag_name || '',
                rarity: item.tags?.find(t => t.category === 'Rarity')?.localized_tag_name || '',
                tradable: item.tradable,
                marketable: item.marketable,
                icon_url: item.icon_url
            })),
            containers: allContainers
        });
    });
}

// ── Storage units ─────────────────────────────────────────────────────────────

function requestStorageUnit(casketId) {
    if (!csgo.haveGCSession) {
        sendError('Not connected to GC yet');
        return;
    }

    csgo.getCasketContents(casketId, (err, items) => {
        if (err) {
            sendError('Storage unit error: ' + err.message);
            return;
        }

        send('storage_unit', {
            casket_id: casketId,
            items: (items || []).map(item => ({
                id: item.id.toString(),
                name: resolveItemName(item.def_index, item.paint_index, item.paint_wear),
                market_hash_name: resolveMarketHashName(item.def_index, item.paint_index, item.paint_wear),
                def_index: item.def_index,
                paint_index: item.paint_index || null,
                paint_seed: item.paint_seed || null,
                paint_wear: item.paint_wear || null,
                quality: item.quality,
                rarity: item.rarity,
                custom_name: item.custom_name || null,
                casket_id: item.casket_id || null,
                stickers: (item.stickers || []).map(s => ({
                    slot: s.slot,
                    sticker_id: s.sticker_id,
                    wear: s.wear
                }))
            }))
        });
    });
}

// ── Stdin command handler ─────────────────────────────────────────────────────

let inputBuffer = '';

process.stdin.setEncoding('utf8');
process.stdin.on('data', (chunk) => {
    inputBuffer += chunk;
    const lines = inputBuffer.split('\n');
    inputBuffer = lines.pop();

    for (const line of lines) {
        if (!line.trim()) continue;
        try {
            const msg = JSON.parse(line);
            handleCommand(msg);
        } catch (e) {
            sendError('Invalid command JSON: ' + e.message);
        }
    }
});

function handleCommand(msg) {
    switch (msg.command) {
        case 'get_inventory':
            requestInventory();
            break;

        case 'get_storage_unit':
            if (!msg.casket_id) {
                sendError('get_storage_unit requires casket_id');
            } else {
                requestStorageUnit(msg.casket_id);
            }
            break;

        case 'add_to_storage_unit':
            if (!msg.casket_id || !msg.item_id) {
                sendError('add_to_storage_unit requires casket_id and item_id');
                return;
            }
            if (!csgo.haveGCSession) {
                sendError('Not connected to GC yet');
                return;
            }
            csgo.addToCasket(msg.casket_id, msg.item_id);
            csgo.once('itemCustomizationNotification', () => {
                send('transfer_complete', {
                    action: 'added',
                    casket_id: msg.casket_id,
                    item_id: msg.item_id
                });
            });
            break;

        case 'remove_from_storage_unit':
            if (!msg.casket_id || !msg.item_id) {
                sendError('remove_from_storage_unit requires casket_id and item_id');
                return;
            }
            if (!csgo.haveGCSession) {
                sendError('Not connected to GC yet');
                return;
            }
            csgo.removeFromCasket(msg.casket_id, msg.item_id);
            csgo.once('itemCustomizationNotification', () => {
                send('transfer_complete', {
                    action: 'removed',
                    casket_id: msg.casket_id,
                    item_id: msg.item_id
                });
            });
            break;

        case 'login_with_web_token':
            if (!msg.token) {
                sendError('login_with_web_token requires token');
                return;
            }
            client.logOn({ refreshToken: msg.token });
            break;

        case 'quit':
            client.logOff();
            process.exit(0);
            break;

        default:
            sendError('Unknown command: ' + msg.command);
    }
}

process.stdin.on('end', () => {
    client.logOff();
    process.exit(0);
});

// ── Startup ───────────────────────────────────────────────────────────────────
// Fetch item database first, then start Steam login.
// If the DB fetch fails we continue anyway — item names fall back to
// "Unknown (def:X)" which is acceptable offline or if fursense.lol is down.

fetchItemDatabase()
    .then(() => startLogin())
    .catch(err => {
        process.stderr.write(`Item DB fetch failed: ${err.message} — continuing without it\n`);
        startLogin().catch(err2 => {
            sendError('Failed to start login: ' + err2.message);
            process.exit(1);
        });
    });


function isCacheValid(cacheFile, maxAgeMs) {
    try {
        const stats = fs.statSync(cacheFile);
        return (Date.now() - stats.mtimeMs) < maxAgeMs;
    } catch (e) {
        return false;
    }
}

function fetchItemDatabase() {
    const ITEMS_CACHE_FILE = path.join(DB_DIR, 'items-extended-cache.json');
    process.stderr.write('Loading item database...\n');

    // Use cache if fresh enough
    if (isCacheValid(ITEMS_CACHE_FILE, ITEMS_CACHE_MAX_AGE_MS)) {
        process.stderr.write('Using cached item database.\n');
        try {
            const body = fs.readFileSync(ITEMS_CACHE_FILE, 'utf8');
            parseItemDatabase(body);
            return Promise.resolve();
        } catch (e) {
            process.stderr.write(`Cache read failed: ${e.message} — fetching fresh\n`);
        }
    }

    process.stderr.write('Fetching fresh item database from fursense.lol...\n');
    return fetchUrl('https://fursense.lol/items-extended.json')
        .then(({ status, body }) => {
            if (status !== 200) throw new Error(`HTTP ${status}`);
            // Save to cache
            try {
                fs.writeFileSync(ITEMS_CACHE_FILE, body);
            } catch (e) {
                process.stderr.write(`Cache write failed: ${e.message}\n`);
            }
            parseItemDatabase(body);
        });
}

function parseItemDatabase(body) {
    const parsed = JSON.parse(body);
    const items = parsed.items || {};

    for (const [name, entry] of Object.entries(items)) {
        if (entry.t === 'skin') {
            if (entry.wi && entry.pi) {
                const key = `${entry.wi}_${entry.pi}`;
                if (!skinsByKey[key]) {
                    const parts = name.split(' | ');
                    const weaponName = parts[0] || '';
                    const patternWithCondition = parts[1] || '';
                    const patternName = patternWithCondition.replace(/\s*\(.*\)$/, '');
                    skinsByKey[key] = {
                        market_hash_name: name,
                        weapon: { name: weaponName },
                        pattern: { name: patternName }
                    };
                }
            }
        } else {
            if (entry.di) {
                const key = String(entry.di);
                if (!defIndexDb[key]) {
                    defIndexDb[key] = {
                        market_hash_name: name,
                        name: name,
                        _type: entry.t
                    };
                }
            }
        }
    }

    dbReady = true;
    process.stderr.write(`Item DB ready: ${Object.keys(skinsByKey).length} skins, ${Object.keys(defIndexDb).length} other items\n`);
}