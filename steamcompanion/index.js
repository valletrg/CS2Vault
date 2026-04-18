const SteamUser = require('steam-user');
const GlobalOffensive = require('node-cs2');
const { LoginSession, EAuthTokenPlatformType } = require('steam-session');
const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const os = require('os');
const https = require('https');

if (process.pkg) {
    process.chdir(path.dirname(process.execPath));
} else {
    process.chdir(__dirname);
}

process.stderr.write(`Working directory: ${process.cwd()}\n`);

const SteamCommunity = require('steamcommunity');
const TradeOfferManager = require('steam-tradeoffer-manager');
const community = new SteamCommunity();
const client = new SteamUser();
const csgo = new GlobalOffensive(client);

let lastCookies = [];

const manager = new TradeOfferManager({
    steam: client,
    community: community,
    language: 'en',
    useAccessToken: true,
    pollInterval: 30000
});

const DB_DIR = process.pkg
    ? path.dirname(process.execPath)
    : __dirname;

const args = process.argv.slice(2);
const profileArgIdx = args.indexOf('--profile');
const PROFILE_DIR = (profileArgIdx >= 0 && args[profileArgIdx + 1])
    ? args[profileArgIdx + 1]
    : DB_DIR;

const REFRESH_TOKEN_FILE = path.join(PROFILE_DIR, 'refresh_token.txt');
const FLOAT_CACHE_FILE = path.join(PROFILE_DIR, 'float_cache.json');
const ITEMS_CACHE_FILE = path.join(DB_DIR, 'items-extended-cache.json');
const ITEMS_CACHE_MAX_AGE_MS = 24 * 60 * 60 * 1000; // 24 hours

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

function fetchUrl(url, redirectCount = 0, extraHeaders = {}) {
    return new Promise((resolve, reject) => {
        if (redirectCount > 5) return reject(new Error('Too many redirects'));

        const request = https.get(url, {
            headers: { 'User-Agent': 'CS2Vault/1.2.0', ...extraHeaders },
            timeout: 15000
        }, (res) => {
            if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
                return fetchUrl(res.headers.location, redirectCount + 1, extraHeaders)
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

function isCacheValid(cacheFile, maxAgeMs) {
    try {
        const stats = fs.statSync(cacheFile);
        return (Date.now() - stats.mtimeMs) < maxAgeMs;
    } catch (e) {
        return false;
    }
}

function fetchItemDatabase() {
    process.stderr.write('Loading item database...\n');

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
            try {
                fs.writeFileSync(ITEMS_CACHE_FILE, body);
            } catch (e) {
                process.stderr.write(`Cache write failed: ${e.message}\n`);
            }
            parseItemDatabase(body);
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

// ── Float cache ───────────────────────────────────────────────────────────────

let floatCache = {};

function loadFloatCache() {
    try {
        if (fs.existsSync(FLOAT_CACHE_FILE))
            floatCache = JSON.parse(fs.readFileSync(FLOAT_CACHE_FILE, 'utf8'));
    } catch (e) {
        process.stderr.write(`Float cache load failed: ${e.message}\n`);
        floatCache = {};
    }
}

function saveFloatCacheEntry(assetId, data) {
    floatCache[assetId] = data;
    try {
        fs.writeFileSync(FLOAT_CACHE_FILE, JSON.stringify(floatCache));
    } catch (e) {
        process.stderr.write(`Float cache save failed: ${e.message}\n`);
    }
}

// ── Inspect link construction ─────────────────────────────────────────────────

function constructInspectLink(item, steamid64) {
    const link = item.actions?.[0]?.link;
    if (!link) return null;
    return link
        .replace('%owner_steamid%', steamid64)
        .replace('%assetid%', item.assetid);
}

// ── Float fetching ────────────────────────────────────────────────────────────

function fetchFloatForItem(inspectLink) {
    const encoded = encodeURIComponent(inspectLink);
    return fetchUrl(`https://inspect.pricempire.com/?url=${encoded}`, 0, {
        'User-Agent': 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/120.0.0.0 Safari/537.36'
    })
        .then(({ status, body }) => {
            if (status !== 200) {
                process.stderr.write(`[float] Pricempire failed: HTTP ${status} — body: ${body.slice(0, 200)}\n`);
                throw new Error(`Pricempire HTTP ${status}`);
            }
            const data = JSON.parse(body);
            if (!data.iteminfo) throw new Error('No iteminfo in Pricempire response');
            return {
                float_value: data.iteminfo.floatvalue,
                paint_seed: data.iteminfo.paintseed,
                paint_index: data.iteminfo.paintindex
            };
        })
        .catch(err => {
            process.stderr.write(`[float] Falling back to Valve for link: ${inspectLink.slice(0, 120)} — reason: ${err.message}\n`);
            return fetchUrl(`https://steamcommunity.com/economy/floatid?url=${encoded}`)
                .then(({ status, body }) => {
                    if (status !== 200) throw new Error(`Valve floatid HTTP ${status}`);
                    const data = JSON.parse(body);
                    if (!data.iteminfo) throw new Error('No iteminfo in Valve response');
                    return {
                        float_value: data.iteminfo.floatvalue,
                        paint_seed: data.iteminfo.paintseed,
                        paint_index: data.iteminfo.paintindex
                    };
                });
        });
}

function processFloatQueue(items) {
    if (items.length === 0) return;
    const [item, ...rest] = items;

    if (items.length === rest.length + 1) {
        // Log first item in each queue run so we can verify link format
        process.stderr.write(`[float] Processing queue — first link: ${item.inspect_link}\n`);
    }

    fetchFloatForItem(item.inspect_link)
        .then(result => {
            saveFloatCacheEntry(item.id, result);
            send('floats', {
                items: [{
                    id: item.id,
                    float_value: result.float_value,
                    paint_seed: result.paint_seed,
                    paint_index: result.paint_index
                }]
            });
        })
        .catch(err => {
            process.stderr.write(`[float] Fetch failed for ${item.id}: ${err.message}\n`);
        })
        .finally(() => {
            setTimeout(() => processFloatQueue(rest), 250);
        });
}

function requestFloats(items) {
    process.stderr.write('[float] Float fetching disabled pending Valve inspect API fix\n');
    return;

    const withLink = items.filter(item => item.inspect_link);
    const withoutLink = items.length - withLink.length;

    // Pre-filter: skip items with no csgo_econ_action_preview in their link
    // (cases, keys, stickers, etc. that can never have floats)
    const floatable = withLink.filter(item =>
        item.inspect_link.includes('csgo_econ_action_preview')
    );
    const skippedNoFloat = withLink.length - floatable.length;

    // Post-construction filter: skip links that still have unresolved placeholders
    // (%20 and other percent-encoded characters are valid and must not be treated as malformed)
    const validLinks = [];
    let skippedMalformed = 0;
    for (const item of floatable) {
        const link = item.inspect_link;
        const unresolved = link.includes('%owner_steamid%') ||
                           link.includes('%assetid%') ||
                           link.includes('%propid:');
        if (unresolved) {
            skippedMalformed++;
        } else {
            validLinks.push(item);
        }
    }

    const uncached = validLinks.filter(item => !floatCache[item.id]);
    const cached = validLinks.length - uncached.length;

    if (uncached.length === 0) return;
    processFloatQueue(uncached);
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

    // No saved token — start QR session
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
    // Guard against anonymous login from expired/invalid token
    if (client.steamID && client.steamID.type === 0) {
        process.stderr.write('Anonymous login detected — token likely expired, deleting\n');
        if (fs.existsSync(REFRESH_TOKEN_FILE)) fs.unlinkSync(REFRESH_TOKEN_FILE);
        sendError('Saved login expired — please sign in again.');
        process.exit(1);
    }

    send('status', { state: 'logged_in', steamid: client.steamID.toString() });
    client.setPersona(SteamUser.EPersonaState.Online);
    client.gamesPlayed([730]);

    client.on('webSession', (sessionId, cookies) => {
        lastCookies = cookies;
        community.setCookies(cookies);
        manager.setCookies(cookies);
        send('status', { state: 'web_session_ready' });
    });

    community.on('sessionExpired', () => {
        process.stderr.write('[session] Session expired, refreshing...\n');
        client.webLogOn();
    });

    setTimeout(() => {
        if (!csgo.haveGCSession) {
            send('status', { state: 'gc_connecting', message: 'Still waiting for GC...' });
        }
    }, 15000);
});

client.on('refreshToken', (token) => {
    // Don't save tokens from anonymous sessions
    if (!client.steamID || client.steamID.type === 0) {
        process.stderr.write('Skipping token save for anonymous session\n');
        return;
    }
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
                icon_url: item.icon_url,
                inspect_link: constructInspectLink(item, client.steamID.toString())
            })),
            containers: allContainers
        });
    });
}

// ── Transfer queue ────────────────────────────────────────────────────────────
// The GC only processes one casket operation at a time; fire them sequentially.

let transferQueue = [];
let transferBusy = false;

function enqueueTransfer(op, casketId, itemId) {
    transferQueue.push({ op, casketId, itemId });
    drainTransferQueue();
}

function drainTransferQueue() {
    if (transferBusy || transferQueue.length === 0) return;

    const { op, casketId, itemId } = transferQueue.shift();
    transferBusy = true;

    if (op === 'add') {
        csgo.addToCasket(casketId, itemId);
    } else {
        csgo.removeFromCasket(casketId, itemId);
    }

    // Safety timeout — if GC never acks, unblock the queue after 10 s
    const timeout = setTimeout(() => {
        process.stderr.write(`[transfer] GC notification timeout for item ${itemId}\n`);
        csgo.removeListener('itemCustomizationNotification', onNotification);
        transferBusy = false;
        drainTransferQueue();
    }, 10000);

    function onNotification() {
        clearTimeout(timeout);
        send('transfer_complete', {
            action: op === 'add' ? 'added' : 'removed',
            casket_id: casketId,
            item_id: itemId
        });
        transferBusy = false;
        drainTransferQueue();
    }

    csgo.once('itemCustomizationNotification', onNotification);
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

// ── Trade offer helpers ───────────────────────────────────────────────────────

function serializeOffer(offer) {
    return {
        id: offer.id,
        partner: offer.partner.toString(),
        message: offer.message || '',
        is_our_offer: offer.isOurOffer,
        items_to_give: (offer.itemsToGive || []).map(item => ({
            assetid: item.assetid,
            market_hash_name: item.market_hash_name,
            appid: item.appid
        })),
        items_to_receive: (offer.itemsToReceive || []).map(item => ({
            assetid: item.assetid,
            market_hash_name: item.market_hash_name,
            appid: item.appid
        })),
        time_created: offer.created ? Math.floor(offer.created.getTime() / 1000) : 0,
        state: offer.state
    };
}

manager.on('newOffer', (offer) => {
    send('new_trade_offer', { offer: serializeOffer(offer) });
});

manager.on('sentOfferChanged', (offer, oldState) => {
    send('trade_offer_changed', { offer_id: offer.id, new_state: offer.state });
});

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
        case 'start_login':
            startLogin().catch(err => {
                sendError('Failed to start login: ' + err.message);
                process.exit(1);
            });
            break;

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
            enqueueTransfer('add', msg.casket_id, msg.item_id);
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
            enqueueTransfer('remove', msg.casket_id, msg.item_id);
            break;

        case 'get_floats':
            if (!msg.items || !Array.isArray(msg.items)) {
                sendError('get_floats requires items array');
                return;
            }
            requestFloats(msg.items);
            break;

        case 'login_with_web_token':
            if (!msg.token || !msg.steamid) {
                sendError('login_with_web_token requires token and steamid');
                return;
            }
            client.logOn({
                webLogonToken: msg.token,
                steamID: msg.steamid
            });
            break;

        case 'get_trade_offers':
            manager.getOffers(TradeOfferManager.EOfferFilter.ActiveOnly, (err, sent, received) => {
                if (err) {
                    sendError('Trade offers error: ' + err.message);
                    return;
                }
                const offers = [...(sent || []), ...(received || [])].map(serializeOffer);
                send('trade_offers', { offers });
            });
            break;

        case 'accept_trade_offer':
            if (!msg.offer_id) {
                sendError('accept_trade_offer requires offer_id');
                return;
            }
            {
                const attemptAccept = (isRetry = false) => {
                    manager.getOffer(msg.offer_id, (err, offer) => {
                        if (err) {
                            send('trade_offer_accepted', {
                                offer_id: msg.offer_id,
                                status: 'error',
                                error: err.message
                            });
                            return;
                        }
                        offer.accept(false, (err, status) => {
                            if (err) {
                                if (!isRetry && err.message === 'Not Logged In') {
                                    process.stderr.write('[trade] not logged in, waiting for session refresh\n');
                                    const retryTimeout = setTimeout(() => {
                                        client.removeAllListeners('webSession');
                                        send('trade_offer_accepted', {
                                            offer_id: msg.offer_id,
                                            status: 'error',
                                            error: 'Session refresh timed out'
                                        });
                                    }, 15000);
                                    client.once('webSession', (sessionId, cookies) => {
                                        clearTimeout(retryTimeout);
                                        community.setCookies(cookies);
                                        manager.setCookies(cookies);
                                        process.stderr.write('[trade] session refreshed, retrying accept\n');
                                        attemptAccept(true);
                                    });
                                    return;
                                }
                                const errLower = err.message.toLowerCase();
                                if (errLower.includes('family view') || errLower.includes('parental')) {
                                    process.stderr.write('[trade] Family View detected, requesting PIN\n');
                                    send('trade_offer_accepted', {
                                        offer_id: msg.offer_id,
                                        status: 'family_view',
                                        error: 'Family View PIN required'
                                    });
                                    return;
                                }
                                send('trade_offer_accepted', {
                                    offer_id: msg.offer_id,
                                    status: 'error',
                                    error: err.message
                                });
                                return;
                            }
                            const normalized = status === 'escrow' ? 'escrow'
                                             : status === 'pending' ? 'pending'
                                             : 'accepted';
                            send('trade_offer_accepted', {
                                offer_id: msg.offer_id,
                                status: normalized
                            });
                        });
                    });
                };
                attemptAccept();
            }
            break;

        case 'cancel_trade_offer':
            if (!msg.offer_id) {
                sendError('cancel_trade_offer requires offer_id');
                return;
            }
            {
                const attemptCancel = (isRetry = false) => {
                    manager.getOffer(msg.offer_id, (err, offer) => {
                        if (err) {
                            send('trade_offer_cancelled', {
                                offer_id: msg.offer_id,
                                status: 'error',
                                error: err.message
                            });
                            return;
                        }
                        offer.cancel((err) => {
                            if (err) {
                                if (!isRetry && err.message === 'Not Logged In') {
                                    process.stderr.write('[trade] not logged in, waiting for session refresh\n');
                                    const retryTimeout = setTimeout(() => {
                                        client.removeAllListeners('webSession');
                                        send('trade_offer_cancelled', {
                                            offer_id: msg.offer_id,
                                            status: 'error',
                                            error: 'Session refresh timed out'
                                        });
                                    }, 15000);
                                    client.once('webSession', (sessionId, cookies) => {
                                        clearTimeout(retryTimeout);
                                        community.setCookies(cookies);
                                        manager.setCookies(cookies);
                                        process.stderr.write('[trade] session refreshed, retrying cancel\n');
                                        attemptCancel(true);
                                    });
                                    return;
                                }
                                send('trade_offer_cancelled', {
                                    offer_id: msg.offer_id,
                                    status: 'error',
                                    error: err.message
                                });
                                return;
                            }
                            send('trade_offer_cancelled', {
                                offer_id: msg.offer_id,
                                status: 'cancelled'
                            });
                        });
                    });
                };
                attemptCancel();
            }
            break;

        case 'send_trade_offer':
            if (!msg.trade_url) {
                sendError('send_trade_offer requires trade_url');
                return;
            }
            {
                const attemptSend = (isRetry = false) => {
                    const offer = manager.createOffer(msg.trade_url);
                    if (msg.message) offer.setMessage(msg.message);
                    for (const assetid of (msg.items_to_give || [])) {
                        offer.addMyItem({ appid: 730, contextid: '2', assetid });
                    }
                    for (const assetid of (msg.items_to_receive || [])) {
                        offer.addTheirItem({ appid: 730, contextid: '2', assetid });
                    }
                    offer.send((err, status) => {
                        if (err) {
                            if (!isRetry && err.message === 'Not Logged In') {
                                process.stderr.write('[trade] not logged in, waiting for session refresh\n');
                                const retryTimeout = setTimeout(() => {
                                    client.removeAllListeners('webSession');
                                    send('trade_offer_sent', {
                                        offer_id: '',
                                        status: 'error',
                                        error: 'Session refresh timed out'
                                    });
                                }, 15000);
                                client.once('webSession', (sessionId, cookies) => {
                                    clearTimeout(retryTimeout);
                                    community.setCookies(cookies);
                                    manager.setCookies(cookies);
                                    process.stderr.write('[trade] session refreshed, retrying send\n');
                                    attemptSend(true);
                                });
                                return;
                            }
                            send('trade_offer_sent', {
                                offer_id: '',
                                status: 'error',
                                error: err.message
                            });
                            return;
                        }
                        send('trade_offer_sent', {
                            offer_id: offer.id,
                            status: 'sent'
                        });
                    });
                };
                attemptSend();
            }
            break;

        case 'parental_unlock':
            if (!msg.pin) {
                sendError('parental_unlock requires pin');
                return;
            }
            community.parentalUnlock(msg.pin, (err) => {
                if (err) {
                    send('parental_unlock', { success: false, error: err.message });
                    return;
                }
                send('parental_unlock', { success: true });
            });
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
// Fetch item database only. Login is triggered by the Qt side via
// 'start_login' or 'login_with_web_token' commands. No QR session
// starts until the user explicitly chooses to log in.

loadFloatCache();

fetchItemDatabase()
    .catch(err => {
        process.stderr.write(`Item DB fetch failed: ${err.message} — continuing without it\n`);
    });