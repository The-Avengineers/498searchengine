import {spawn} from 'node:child_process';
import type {CliConfig} from './types.js';

export type OpenPhase =
	| {kind: 'active'; url: string; startedAt: Date}
	| {kind: 'error'; message: string};

export type OpenSession = {
	cleanup: () => void;
};

function openBrowser(url: string, platform: NodeJS.Platform) {
	const cmd = platform === 'darwin' ? 'open' : 'xdg-open';
	const child = spawn(cmd, [url], {detached: true, stdio: 'ignore'});
	child.unref();
}

export async function launchBrowser(
	config: CliConfig,
	_localPort: number,
	onPhase: (phase: OpenPhase) => void,
): Promise<OpenSession> {
	function cleanup() {}

	try {
		const url = config.serverUrl;
		openBrowser(url, config.platform);
		onPhase({kind: 'active', url, startedAt: new Date()});
	} catch (error: unknown) {
		onPhase({
			kind: 'error',
			message: error instanceof Error ? error.message : 'Unknown error launching browser.',
		});
	}

	return {cleanup};
}
