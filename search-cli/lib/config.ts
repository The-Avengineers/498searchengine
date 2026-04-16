import process from 'node:process';

import type {CliConfig, SshConfig} from './types.js';

type ConfigEnvironment = NodeJS.ProcessEnv;

type RuntimeHints = {
	platform: NodeJS.Platform;
	linkSupport: boolean;
	italicSupport: boolean;
};

function normalizeUrl(url: string) {
	return url.endsWith('/') ? url.slice(0, -1) : url;
}

function detectLinkSupport(environment: ConfigEnvironment) {
	if (!process.stdout.isTTY) {
		return false;
	}

	if (environment['TERM'] === 'dumb') {
		return false;
	}

	return true;
}

function detectItalicSupport(environment: ConfigEnvironment) {
	if (!process.stdout.isTTY) {
		return false;
	}

	if (environment['TERM'] === 'dumb') {
		return false;
	}

	return true;
}

function resolveSshConfig(environment: ConfigEnvironment): SshConfig | undefined {
	const host = environment['AVENGINE_SSH_HOST'];
	if (!host) {
		return undefined;
	}

	const portRaw = environment['AVENGINE_SSH_PORT'];
	const port = portRaw ? Number.parseInt(portRaw, 10) : 2222;

	return {
		host,
		port: Number.isFinite(port) && port > 0 ? port : 2222,
		user: environment['AVENGINE_SSH_USER'] ?? 'engine',
		keyPath: environment['AVENGINE_SSH_KEY'],
	};
}

export function resolveCliConfig(
	environment: ConfigEnvironment = process.env,
	runtimeHints?: Partial<RuntimeHints>,
): CliConfig {
	const ssh = resolveSshConfig(environment);
	const explicitServerUrl = environment['AVENGINE_SERVER_URL'];
	const serverUrl = explicitServerUrl
		? normalizeUrl(explicitServerUrl)
		: 'https://theavengine.com';

	return {
		serverUrl,
		runtimeBinRoot: environment['AVENGINE_RUNTIME_BIN_ROOT'] ?? '/avengine/bin',
		dataRoot: environment['AVENGINE_DATA_ROOT'] ?? '/data',
		configRoot: environment['AVENGINE_CONFIG_ROOT'] ?? '/config',
		platform: runtimeHints?.platform ?? process.platform,
		linkSupport: runtimeHints?.linkSupport ?? detectLinkSupport(environment),
		italicSupport:
			runtimeHints?.italicSupport ?? detectItalicSupport(environment),
		ssh,
	};
}
