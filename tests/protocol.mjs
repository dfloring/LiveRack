// Exercises LiveRack's /live server protocol against a running preview build.
import dgram from 'node:dgram';

const host = process.argv[2] || 'localhost:8090';
const log = (...a) => console.log('LIVE', ...a);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// --- Beacon: is LiveRack announcing itself?
const beacon = await new Promise((resolve) => {
  const s = dgram.createSocket({ type: 'udp4', reuseAddr: true });
  const t = setTimeout(() => { s.close(); resolve(null); }, 5000);
  s.on('message', (msg, rinfo) => { clearTimeout(t); s.close(); resolve({ ...JSON.parse(msg), from: rinfo.address }); });
  s.bind(47810);
});
log('beacon', JSON.stringify(beacon));

// --- Live connection
const ws = new WebSocket(`ws://${host}/live?live=1`);
let hello = null, state = null, states = 0, meters = 0, lastMeters = null;
const replies = {};
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.t === 'hello') hello = m;
  if (m.t === 'state') { state = m.data; states++; }
  if (m.t === 'meters') { meters++; lastMeters = m.data; }
  if (m.t === 'reply') replies[m.id] = m;
};
await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
const send = (o) => ws.send(JSON.stringify(o));

await sleep(1500);
log('hello', JSON.stringify(hello));
log('state racks', state?.racks?.length, 'drivers', state?.drivers?.length, 'snapshots', state?.snapshots?.length,
    'hot', JSON.stringify(state?.hot), 'server', JSON.stringify(state?.server));
log('meters in 1.5 s', meters, 'sample', JSON.stringify(lastMeters?.racks?.[0]), 'drivers', JSON.stringify(lastMeters?.drivers));

// Commands: bypass rack 1 and see the state change pushed
const before = states;
send({ t: 'cmd', cmd: 'rackBypass', rack: 1, value: 1, id: 1 });
await sleep(400);
log('rack 1 bypass after cmd', state.racks[1].bypass, 'state pushes', states - before, 'reply', JSON.stringify(replies[1]));
send({ t: 'cmd', cmd: 'rackBypass', rack: 1, value: 0 });

// Watching a plugin: its parameters come with the state
send({ t: 'query', q: 'live=1&view=0:1' });
await sleep(400);
log('view', state.view?.name, 'params', state.view?.params?.length);

// Next / previous rack
const sel = state.selected;
send({ t: 'cmd', cmd: 'rackStep', dir: 1 });
await sleep(400);
log('rackStep from', sel, 'to', state.selected);

// Snapshots
send({ t: 'cmd', cmd: 'sceneStore', number: 42, name: 'From client' });
await sleep(500);
log('stored', JSON.stringify(state.snapshots.find((s) => s.number === 42)));
send({ t: 'cmd', cmd: 'sceneRename', number: 42, name: 'Renamed' });
await sleep(400);
log('renamed', JSON.stringify(state.snapshots.find((s) => s.number === 42)));
send({ t: 'cmd', cmd: 'sceneDelete', number: 42 });
await sleep(400);
log('deleted', !state.snapshots.some((s) => s.number === 42));

// Hot plugin and latency group
send({ t: 'cmd', cmd: 'hot', rack: 6, slot: 1, on: true });
send({ t: 'cmd', cmd: 'latencyGroup', rack: 6, group: 2 });
await sleep(400);
log('hot has [6,1]', state.hot.some((h) => h[0] === 6 && h[1] === 1), 'rack 6 group', state.rackInfo[6].group);

// Server settings
send({ t: 'cmd', cmd: 'server', multiCore: false });
await sleep(400);
log('multiCore off', state.server.multiCore === false);
send({ t: 'cmd', cmd: 'server', multiCore: true });

// Gets
send({ t: 'get', what: 'library', id: 10 });
send({ t: 'get', what: 'devices', id: 11 });
send({ t: 'get', what: 'shows', id: 12 });
await sleep(800);
log('library', replies[10]?.data?.library?.length, 'devices', JSON.stringify(replies[11]?.data?.devices?.[0]?.types?.map((t) => t.name)),
    'shows', replies[12]?.data?.shows?.length);

// Snapshot detail, partial store, duplicate, renumber, scope
send({ t: 'cmd', cmd: 'sceneStore', number: 50, name: 'Detail' });
await sleep(300);
send({ t: 'cmd', cmd: 'rackBypass', rack: 2, value: 1 });
await sleep(300);
send({ t: 'get', what: 'snapshot', number: 50, id: 20 });
await sleep(500);
const det = replies[20]?.data?.snapshot;
log('snapshot 50 differences', JSON.stringify(det?.differences), 'inScope racks', det?.inScope?.length);
send({ t: 'cmd', cmd: 'sceneStore', number: 50, racks: [2] });
await sleep(300);
send({ t: 'get', what: 'snapshot', number: 50, id: 21 });
await sleep(500);
log('after storing only rack 2 bypass:', replies[21]?.data?.snapshot?.differences?.length, 'differences (expect 0)');
send({ t: 'cmd', cmd: 'rackBypass', rack: 2, value: 0 });
send({ t: 'cmd', cmd: 'sceneDuplicate', number: 50 });
await sleep(300);
send({ t: 'cmd', cmd: 'sceneRenumber', number: 50, to: 60 });
await sleep(300);
log('snapshots now', JSON.stringify(state.snapshots.map((s) => s.number + ':' + s.name)));
send({ t: 'cmd', cmd: 'sceneScope', number: 60, rack: 0, slot: 1, in: false, bpm: false });
send({ t: 'get', what: 'snapshot', number: 60, id: 22 });
await sleep(500);
log('scope of 60', JSON.stringify(replies[22]?.data?.snapshot?.scope), 'rack 0', JSON.stringify(replies[22]?.data?.snapshot?.inScope?.[0]));
send({ t: 'cmd', cmd: 'midiSnapshots', follow: true, first: 1, channel: 0 });
await sleep(300);
log('midi snapshots', JSON.stringify(state.midiSnapshots), 'trigger for 60:', state.snapshots.find((s) => s.number === 60)?.midi);

// Ping
let pong = false;
ws.addEventListener('message', (e) => { if (JSON.parse(e.data).t === 'pong') pong = true; });
send({ t: 'ping' });
await sleep(300);
log('pong', pong);

// A second client at the same time, and the old polling endpoint still works
const ws2 = new WebSocket(`ws://${host}/live`);
let hello2 = null;
ws2.onmessage = (e) => { const m = JSON.parse(e.data); if (m.t === 'hello') hello2 = m; };
const polled = await (await fetch(`http://${host}/state`)).json();
await sleep(500);
log('second client hello', !!hello2, 'poll racks', polled.racks.length);
ws2.close();
ws.close();
await sleep(200);
process.exit(0);
