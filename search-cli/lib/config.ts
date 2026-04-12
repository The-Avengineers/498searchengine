import process from 'node:process';

import type {CliConfig} from './types.js';

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

export function resolveCliConfig(
	environment: ConfigEnvironment = process.env,
	runtimeHints?: Partial<RuntimeHints>,
): CliConfig {
	return {
		serverUrl: normalizeUrl(
			environment['AVENGINE_SERVER_URL'] ?? 'http://127.0.0.1:8080',
		),
		runtimeBinRoot: environment['AVENGINE_RUNTIME_BIN_ROOT'] ?? '/avengine/bin',
		dataRoot: environment['AVENGINE_DATA_ROOT'] ?? '/data',
		configRoot: environment['AVENGINE_CONFIG_ROOT'] ?? '/config',
		platform: runtimeHints?.platform ?? process.platform,
		linkSupport: runtimeHints?.linkSupport ?? detectLinkSupport(environment),
		italicSupport:
			runtimeHints?.italicSupport ?? detectItalicSupport(environment),
	};
}
