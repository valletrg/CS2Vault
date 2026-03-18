const SteamUser = require('steam-user');
const GlobalOffensive = require('node-cs2');
const { LoginSession, EAuthTokenPlatformType } = require('steam-session');
const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const os = require('os');

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
// Not cryptographically perfect but stops casual plaintext reading
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
// Key: "weapon_id_paint_index" -> skin entry (both as strings)
// This uniquely identifies a skin on a specific weapon type.

let skinsByKey = {};

try {
    const skinsRaw = JSON.parse(fs.readFileSync(path.join(DB_DIR, 'skins.json'), 'utf8'));
    skinsRaw.forEach(skin => {
        if (skin.paint_index && skin.weapon?.weapon_id) {
            const key = `${skin.weapon.weapon_id}_${skin.paint_index}`;
            if (!skinsByKey[key]) {
                skinsByKey[key] = skin;
            }
        }
    });
    process.stderr.write(`Loaded ${Object.keys(skinsByKey).length} skins\n`);
} catch (e) {
    process.stderr.write(`SKINS ERROR: ${e.message}\n${e.stack}\n`);
}

let defIndexDb = {};

const defIndexFiles = [
    { file: path.join(DB_DIR, 'crates.json'), type: 'crate' },
    { file: path.join(DB_DIR, 'graffiti.json'), type: 'graffiti' },
    { file: path.join(DB_DIR, 'agents.json'), type: 'agent' },
    { file: path.join(DB_DIR, 'patches.json'), type: 'patch' },
    { file: path.join(DB_DIR, 'collectibles.json'), type: 'collectible' },
];

for (const { file, type } of defIndexFiles) {
    try {
        const raw = JSON.parse(fs.readFileSync(file, 'utf8'));
        raw.forEach(item => {
            if (item.def_index) {
                const key = String(item.def_index);
                if (!defIndexDb[key]) {
                    defIndexDb[key] = { ...item, _type: type };
                }
            }
        });
        process.stderr.write(`Loaded ${raw.length} ${type} entries\n`);
    } catch (e) {
        process.stderr.write(`Skipping ${file}: ${e.message}\n`);
    }
}



function wearFromFloat(f) {
    if (f === null || f === undefined) return '';
    if (f < 0.07) return 'Factory New';
    if (f < 0.15) return 'Minimal Wear';
    if (f < 0.38) return 'Field-Tested';
    if (f < 0.45) return 'Well-Worn';
    return 'Battle-Scarred';
}

function resolveItemName(defIndex, paintIndex, paintWear) {
    // Known defindexes not in any ByMykel file
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
            // Build market hash name with the correct wear condition
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
    fs.writeFileSync(REFRESH_TOKEN_FILE, token);
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
            // Emit as inventory signal so the UI combo box gets populated
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

        // Also include GC-known caskets (storage units don't appear in web inventory)
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

//error catch
startLogin().catch(err => {
    sendError('Failed to start login: ' + err.message);
    process.exit(1);
});