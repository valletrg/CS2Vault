const SteamUser = require('steam-user');
const GlobalOffensive = require('node-cs2');
const { LoginSession, EAuthTokenPlatformType } = require('steam-session');
const { execSync } = require('child_process');
const fs = require('fs');

const client = new SteamUser();
const csgo = new GlobalOffensive(client);
const SteamCommunity = require('steamcommunity');
const community = new SteamCommunity();

const REFRESH_TOKEN_FILE = './refresh_token.txt';
let itemsDb = {};

try {
    const raw = fs.readFileSync('./items.json', 'utf8');
    const items = JSON.parse(raw);
    // Build a lookup map by defindex
    items.forEach(item => {
        if (item.item_name) {
            itemsDb[item.defindex] = item;
        }
    });
    send('status', { state: 'items_db_loaded', count: Object.keys(itemsDb).length });
} catch (e) {
    send('status', { state: 'items_db_missing', message: 'Run: curl -o items.json <url>' });
}


// ── Item database ─────────────────────────────────────────────────────────────

let skinsByPaintIndex = {};  // paint_index -> skin entry
let cratesByDefIndex = {};   // def_index -> crate entry

try {
    const skinsRaw = JSON.parse(fs.readFileSync('./skins.json', 'utf8'));
    // skins_not_grouped has one entry per skin+condition combo
    // We only need one entry per paint_index to get the name, so just take the first match
    skinsRaw.forEach(skin => {
        if (skin.paint_index && !skinsByPaintIndex[skin.paint_index]) {
            skinsByPaintIndex[skin.paint_index] = skin;
        }
    });
    process.stderr.write(`Loaded ${Object.keys(skinsByPaintIndex).length} skins\n`);
} catch (e) {
    process.stderr.write(`Failed to load skins.json: ${e.message}\n`);
}

try {
    const cratesRaw = JSON.parse(fs.readFileSync('./crates.json', 'utf8'));
    cratesRaw.forEach(crate => {
        if (crate.def_index) {
            cratesByDefIndex[crate.def_index] = crate;
        }
    });
    process.stderr.write(`Loaded ${Object.keys(cratesByDefIndex).length} crates\n`);
} catch (e) {
    process.stderr.write(`Failed to load crates.json: ${e.message}\n`);
}

function resolveItemName(defIndex, paintIndex, paintWear) {
    // If it has a paint_index it's a skin
    if (paintIndex) {
        const skin = skinsByPaintIndex[paintIndex];
        if (skin) {
            // Get the weapon name from the skin entry
            const weaponName = skin.weapon?.name || '';
            const skinName = skin.pattern?.name || '';
            // Determine condition from paint_wear
            let condition = '';
            if (paintWear !== null && paintWear !== undefined) {
                if (paintWear < 0.07) condition = 'Factory New';
                else if (paintWear < 0.15) condition = 'Minimal Wear';
                else if (paintWear < 0.38) condition = 'Field-Tested';
                else if (paintWear < 0.45) condition = 'Well-Worn';
                else condition = 'Battle-Scarred';
            }
            return weaponName && skinName
                ? `${weaponName} | ${skinName} (${condition})`
                : skin.name || `Unknown Skin (paint:${paintIndex})`;
        }
        return `Unknown Skin (paint:${paintIndex})`;
    }

    // No paint_index - try to resolve as a crate/case
    if (defIndex) {
        const crate = cratesByDefIndex[defIndex];
        if (crate) return crate.market_hash_name || crate.name;
    }

    return `Unknown Item (def:${defIndex})`;
}


function send(type, data) {
    process.stdout.write(JSON.stringify({ type, ...data }) + '\n');
}

function sendError(message) {
    send('error', { message });
}

async function startLogin() {
    send('status', { state: 'starting' });

    if (fs.existsSync(REFRESH_TOKEN_FILE)) {
        const refreshToken = fs.readFileSync(REFRESH_TOKEN_FILE, 'utf8').trim();
        send('status', { state: 'using_saved_token' });
        client.logOn({ refreshToken });
        return;
    }

    send('status', { state: 'creating_session' });
    const session = new LoginSession(EAuthTokenPlatformType.SteamClient);
    send('status', { state: 'session_created' });

    session.on('remoteInteraction', () => {
        send('status', { state: 'qr_scanned', message: 'QR scanned! Approve in Steam app...' });
    });

    session.on('authenticated', async () => {
        const refreshToken = session.refreshToken;
        fs.writeFileSync(REFRESH_TOKEN_FILE, refreshToken);
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
        execSync("qrencode -t UTF8 '" + result.qrChallengeUrl + "'", { stdio: 'inherit' });
    } catch (e) {
        send('status', { state: 'qr_fallback', url: result.qrChallengeUrl });
    }
}

client.on('loggedOn', () => {
    send('status', { state: 'logged_in', steamid: client.steamID.toString() });
    client.setPersona(SteamUser.EPersonaState.Online);
    client.gamesPlayed([730]);

    // Give steamcommunity the session cookies so it can make authenticated requests
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

csgo.on('connectedToGC', () => {
    send('status', { state: 'gc_ready' });

    // Give the GC inventory a moment to populate
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
    }, 3000);
});

csgo.on('disconnectedFromGC', (reason) => {
    send('status', { state: 'gc_disconnected', reason });
});

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
            containers: containers.map(c => ({
                id: c.assetid,
                name: c.market_hash_name
            }))
        });
    });
}

function mapItem(item) {
    return {
        id: item.assetid,
        market_hash_name: item.market_hash_name,
        name: item.name,
        type: item.type,
        rarity: item.tags?.find(t => t.category === 'Rarity')?.localized_tag_name || '',
        exterior: item.tags?.find(t => t.category === 'Exterior')?.localized_tag_name || '',
        tradable: item.tradable,
        marketable: item.marketable,
        icon_url: item.icon_url
    };
}

function mapContainer(item) {
    return {
        id: item.assetid,
        name: item.market_hash_name,
        context: item.contextid
    };
}

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
                name: item.market_hash_name ||
                    skinsByPaintIndex[item.paint_index]?.market_hash_name ||
                    cratesByDefIndex[item.def_index]?.market_hash_name ||
                    `Unknown (def:${item.def_index})`,
                market_hash_name: skinsByPaintIndex[item.paint_index]?.market_hash_name ||
                    cratesByDefIndex[item.def_index]?.market_hash_name || null,
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

startLogin().catch(err => {
    sendError('Failed to start login: ' + err.message);
    process.exit(1);
});


function resolveItemName(defIndex, paintIndex) {
    const item = itemsDb[defIndex];
    if (!item) return `Unknown (def:${defIndex})`;
    if (!paintIndex) return item.item_name;
    // Find the skin by paint_index
    const paint = item.skins?.find(s => s.paint_index === paintIndex);
    return paint ? `${item.item_name} | ${paint.name}` : item.item_name;
}
