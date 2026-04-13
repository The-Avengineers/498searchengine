import http from 'node:http';
import https from 'node:https';
import process from 'node:process';
import {spawn} from 'node:child_process';
import type {ChildProcess} from 'node:child_process';

import {createSshTunnel, executeRemoteScript} from './ssh.js';
import type {CliConfig} from './types.js';

const REMOTE_SERVER_PORT = 8080;
const POLL_INTERVAL_MS = 500;
const POLL_TIMEOUT_MS = 15_000;

export type OpenPhase =
	| {kind: 'starting-server'}
	| {kind: 'opening-tunnel'}
	| {kind: 'waiting-for-server'; url: string}
	| {kind: 'active'; url: string; startedAt: Date}
	| {kind: 'error'; message: string};

export type OpenSession = {
	cleanup: () => void;
};

// Poll a URL until it returns any HTTP response (or times out).
async function waitForServer(url: string): Promise<boolean> {
	const deadline = Date.now() + POLL_TIMEOUT_MS;
	const transport = url.startsWith('https') ? https : http;

	return new Promise(resolve => {
		function attempt() {
			if (Date.now() > deadline) {
				resolve(false);
				return;
			}

			const req = transport.get(url, res => {
				res.resume();
				resolve(true);
			});

			req.setTimeout(POLL_INTERVAL_MS);
			req.on('error', () => {
				setTimeout(attempt, POLL_INTERVAL_MS);
			});
			req.on('timeout', () => {
				req.destroy();
				setTimeout(attempt, POLL_INTERVAL_MS);
			});
		}

		attempt();
	});
}

function openBrowser(url: string, platform: NodeJS.Platform) {
	const cmd = platform === 'darwin' ? 'open' : 'xdg-open';
	const child = spawn(cmd, [url], {detached: true, stdio: 'ignore'});
	child.unref();
}

// Start the search server in daemon mode on the remote container via SSH.
// The start-search-server.sh script is idempotent — safe to call when already running.
async function startRemoteSearchServer(config: CliConfig): Promise<void> {
	if (!config.ssh) return;
	const scriptPath = `${config.runtimeBinRoot}/start-search-server.sh`;
	await executeRemoteScript(scriptPath, config.ssh, [
		String(REMOTE_SERVER_PORT),
		`${config.dataRoot}/corpus-index/current`,
		'daemon',
	]);
}

async function startLocalSearchServer(config: CliConfig): Promise<void> {
	const scriptPath = `${config.runtimeBinRoot}/start-search-server.sh`;
	return new Promise<void>((resolve, reject) => {
		const child = spawn(
			scriptPath,
			[String(REMOTE_SERVER_PORT), `${config.dataRoot}/corpus-index/current`, 'daemon'],
			{
				env: {
					...process.env,
					AVENGINE_BIN_ROOT: config.runtimeBinRoot,
					AVENGINE_DATA_ROOT: config.dataRoot,
				},
				stdio: 'ignore',
			},
		);
		child.on('close', code => {
			if (code === 0 || code === null) resolve();
			else reject(new Error(`start-search-server.sh exited with code ${code}`));
		});
		child.on('error', reject);
	});
}

export async function launchBrowser(
	config: CliConfig,
	localPort: number,
	onPhase: (phase: OpenPhase) => void,
): Promise<OpenSession> {
	let tunnel: ChildProcess | null = null;

	function cleanup() {
		if (tunnel && !tunnel.killed) {
			tunnel.kill('SIGTERM');
		}
	}

	try {
		if (config.ssh) {
			onPhase({kind: 'starting-server'});
			try {
				await startRemoteSearchServer(config);
			} catch {
				// Non-fatal — server may already be running
			}

			onPhase({kind: 'opening-tunnel'});
			tunnel = createSshTunnel(localPort, REMOTE_SERVER_PORT, config.ssh);

			const stderrChunks: string[] = [];
			tunnel.stderr?.on('data', (chunk: Buffer) => {
				stderrChunks.push(chunk.toString());
			});

			tunnel.on('close', code => {
				if (code !== 0 && code !== null) {
					const detail = stderrChunks.join('').trim();
					onPhase({
						kind: 'error',
						message: detail.length > 0 ? detail : `Tunnel closed unexpectedly (code ${code})`,
					});
				}
			});

			const localUrl = `http://localhost:${localPort}`;
			onPhase({kind: 'waiting-for-server', url: localUrl});

			const reachable = await waitForServer(`${localUrl}/api/search?q=ping&limit=1`);
			if (!reachable) {
				cleanup();
				onPhase({
					kind: 'error',
					message: `Server unreachable at ${localUrl} after ${POLL_TIMEOUT_MS / 1000}s. Is the index built?`,
				});
				return {cleanup};
			}

			openBrowser(localUrl, config.platform);
			onPhase({kind: 'active', url: localUrl, startedAt: new Date()});
		} else {
			onPhase({kind: 'starting-server'});
			try {
				await startLocalSearchServer(config);
			} catch {
				// Non-fatal if already running
			}

			const url = config.serverUrl;
			onPhase({kind: 'waiting-for-server', url});

			const reachable = await waitForServer(`${url}/api/search?q=ping&limit=1`);
			if (!reachable) {
				onPhase({
					kind: 'error',
					message: `Server unreachable at ${url} after ${POLL_TIMEOUT_MS / 1000}s.`,
				});
				return {cleanup};
			}

			openBrowser(url, config.platform);
			onPhase({kind: 'active', url, startedAt: new Date()});
		}
	} catch (error: unknown) {
		cleanup();
		onPhase({
			kind: 'error',
			message: error instanceof Error ? error.message : 'Unknown error launching browser.',
		});
	}

	return {cleanup};
}
