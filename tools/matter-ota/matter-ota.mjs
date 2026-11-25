import "@matter/nodejs";
import { Environment, Logger, LogLevel, Seconds, StorageService } from "@matter/general";
import {
  ChangeNotificationService,
  ClusterBehavior,
  ControllerBehavior,
  DclBehavior,
  Endpoint,
  NetworkClient,
  ServerNode,
  SoftwareUpdateManager,
} from "@matter/node";
import { BasicInformationClient } from "@matter/node/behaviors/basic-information";
import { OtaProviderEndpoint } from "@matter/node/endpoints/ota-provider";
import { FabricAuthority, PeerAddress } from "@matter/protocol";
import { FabricIndex, ManualPairingCodeCodec, NodeId } from "@matter/types";
import { GeneralCommissioning } from "@matter/types/clusters";
import { createReadStream } from "node:fs";
import { resolve } from "node:path";
import { Readable } from "node:stream";

const ID = "shell-0";
const LABEL = "matter.js Shell";
const REQUESTOR_CLUSTER = 0x2a;
const IDLE = 1;
const QUERYING = 2;
const ATTEMPTS = 8;
const RETRY_DELAY_MS = 20_000;
const CONNECT_TIMEOUT_MS = 120_000;
const UPDATE_TIMEOUT_MS = 600_000;

Logger.level = process.env.MATTER_OTA_DEBUG ? LogLevel.DEBUG : LogLevel.ERROR;

const [command, argument] = process.argv.slice(2);

function fail(message) {
  console.error(message);
  process.exit(1);
}

function timeout(ms, what) {
  return new Promise((_, reject) => setTimeout(() => reject(new Error(`${what} timed out`)), ms).unref());
}

async function startController() {
  const environment = new Environment(ID, Environment.default);
  await environment.get(StorageService).open(ID);
  const node = await ServerNode.create(ServerNode.RootEndpointWithoutGroupcast.with(ControllerBehavior), {
    environment,
    id: ID,
    network: { ble: false, tcp: true, autoStartCommissionedPeers: false },
    basicInformation: { productName: LABEL },
    controller: { adminFabricLabel: LABEL, ble: false },
    commissioning: { enabled: false },
    subscriptions: { persistenceEnabled: false },
  });
  const provider = new Endpoint(OtaProviderEndpoint, { id: "ota-provider" });
  await node.add(provider);
  await (await node.env.load(FabricAuthority)).defaultFabric({ adminFabricLabel: LABEL });
  await node.start();
  await provider.setStateOf(SoftwareUpdateManager, { allowTestOtaImages: true });
  return { node, provider };
}

function onlyPeer(node) {
  const peers = node.peers.commissioned;
  if (peers.length === 0) fail("Not paired yet. Run: tools/matter-ota.sh pair <pairing-code>");
  if (peers.length > 1) fail("More than one purifier is paired to this controller.");
  return peers[0];
}

async function connect(peer) {
  await peer.setStateOf(NetworkClient, {
    autoSubscribe: true,
    defaultSubscription: { minIntervalFloor: Seconds(1), maxIntervalCeiling: Seconds(60) },
  });
  if (!peer.lifecycle.isConnected) {
    const connected = new Promise((done) => {
      const listener = () => {
        if (peer.lifecycle.isConnected) {
          peer.lifecycle.connectionStateChanged.off(listener);
          done();
        }
      };
      peer.lifecycle.connectionStateChanged.on(listener);
    });
    await (peer.stateOf(NetworkClient).isDisabled ? peer.enable() : peer.start());
    await Promise.race([connected, timeout(CONNECT_TIMEOUT_MS, "Connecting to the purifier")]);
  }
}

async function pair(node, code) {
  if (node.peers.commissioned.length > 0) fail("Already paired. Remove devices/<name>/matter to pair again.");
  const { shortDiscriminator, passcode, vendorId, productId } = ManualPairingCodeCodec.decode(code.replace(/[\s-]/g, ""));
  const peer = await node.peers.commission({
    shortDiscriminator,
    passcode,
    vendorId,
    productId,
    autoSubscribe: false,
    regulatoryLocation: GeneralCommissioning.RegulatoryLocationType.Outdoor,
    regulatoryCountryCode: "XX",
    discoveryCapabilities: { ble: false, onIpNetwork: true },
    onAttestationFailure: () => true,
  });
  console.log(`Paired as node ${peer.peerAddress.nodeId}.`);
}

async function storeImage(node, file) {
  const ota = await node.act((agent) => agent.get(DclBehavior).otaUpdateService);
  const stream = () => Readable.toWeb(createReadStream(file));
  const info = await ota.updateInfoFromStream(stream(), `file://${file}`);
  await ota.store(stream(), info, "local");
  return info;
}

function watchRequestor(node, onState) {
  const listener = (change) => {
    if (change.kind !== "event" || !ClusterBehavior.is(change.behavior)) return;
    if (change.behavior.cluster.id !== REQUESTOR_CLUSTER || change.event.propertyName !== "stateTransition") return;
    onState(change.payload);
  };
  const notifications = node.env.get(ChangeNotificationService);
  notifications.change.on(listener);
  return () => notifications.change.off(listener);
}

function attempt(node, provider, peer, info) {
  const events = provider.eventsOf(SoftwareUpdateManager);
  return new Promise((done, reject) => {
    let unwatch = () => {};
    const finish = (result) => {
      events.updateDone.off(onDone);
      events.updateFailed.off(onFailed);
      unwatch();
      done(result);
    };
    const onDone = () => finish("done");
    const onFailed = () => finish("failed");
    events.updateDone.on(onDone);
    events.updateFailed.on(onFailed);
    unwatch = watchRequestor(node, ({ previousState, newState }) => {
      if (previousState === QUERYING && newState === IDLE) finish("refused");
    });
    const basic = peer.stateOf(BasicInformationClient);
    provider
      .act((agent) =>
        agent.get(SoftwareUpdateManager).forceUpdate(PeerAddress({ nodeId: peer.peerAddress.nodeId, fabricIndex: FabricIndex(1) }), {
          vendorId: basic.vendorId,
          productId: basic.productId,
          targetSoftwareVersion: info.softwareVersion,
        }),
      )
      .catch(reject);
  });
}

async function update(node, provider, file) {
  const info = await storeImage(node, file);
  console.log(`Image ${info.softwareVersionString} (version ${info.softwareVersion}).`);
  const peer = onlyPeer(node);
  await connect(peer);
  const basic = peer.stateOf(BasicInformationClient);
  if (basic.vendorId !== info.vid || basic.productId !== info.pid) {
    fail(`The image is for vendor 0x${info.vid.toString(16)} product 0x${info.pid.toString(16)}; the purifier is 0x${basic.vendorId.toString(16)}/0x${basic.productId.toString(16)}.`);
  }
  if (info.softwareVersion <= basic.softwareVersion) {
    fail(`The purifier runs version ${basic.softwareVersion} (${basic.softwareVersionString}). Matter only installs a higher version.`);
  }
  console.log(`Sending to the purifier, now on ${basic.softwareVersionString}. About a minute, then it reboots.`);
  for (let n = 1; n <= ATTEMPTS; n++) {
    const result = await Promise.race([attempt(node, provider, peer, info), timeout(UPDATE_TIMEOUT_MS, "The update")]);
    if (result === "done") {
      console.log(`Updated to ${info.softwareVersionString}.`);
      return;
    }
    if (result === "failed") fail("The purifier reported the update failed. Its log has the reason: tools/log.sh");
    if (n < ATTEMPTS) {
      if (n === 1) console.log("Not taken yet: the purifier refuses until its own last update is confirmed, up to two minutes after booting. Retrying every 20 seconds.");
      await new Promise((r) => setTimeout(r, RETRY_DELAY_MS));
    }
  }
  fail("The purifier kept refusing. Check its log: tools/log.sh");
}

if (command !== "pair" && command !== "update") fail("usage: matter-ota.mjs pair <code> | update <image>");
if (!argument) fail(`${command} needs an argument`);

const { node, provider } = await startController();
try {
  if (command === "pair") await pair(node, argument);
  else await update(node, provider, resolve(argument));
} catch (error) {
  console.error(error.message ?? error);
  process.exitCode = 1;
} finally {
  await node.close();
}
process.exit(process.exitCode ?? 0);
