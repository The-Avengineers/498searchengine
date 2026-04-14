import process from 'node:process';

import {readPersistentConfigSafe} from './persistent-config.js';
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

function resolveSshConfig(
	environment: ConfigEnvironment,
): SshConfig | undefined {
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
	const platform = runtimeHints?.platform ?? process.platform;
	const persistentConfig = readPersistentConfigSafe(environment, platform);
	const envSsh = resolveSshConfig(environment);
	const persistentSsh = persistentConfig.ssh?.host
		? {
				host: persistentConfig.ssh.host,
				port: persistentConfig.ssh.port ?? 2222,
				user: persistentConfig.ssh.user ?? 'engine',
				keyPath: persistentConfig.ssh.keyPath,
		  }
		: undefined;
	const ssh = envSsh ?? persistentSsh;

	// Auto-derive serverUrl from SSH host when not explicitly set
	const explicitServerUrl = environment['AVENGINE_SERVER_URL'];
	const serverUrl = explicitServerUrl
		? normalizeUrl(explicitServerUrl)
		: persistentConfig.serverUrl
		? normalizeUrl(persistentConfig.serverUrl)
		: ssh
		? `http://${ssh.host}:8080`
		: 'http://127.0.0.1:8080';

	return {
		serverUrl,
		runtimeBinRoot: environment['AVENGINE_RUNTIME_BIN_ROOT'] ?? '/avengine/bin',
		dataRoot: environment['AVENGINE_DATA_ROOT'] ?? '/data',
		configRoot: environment['AVENGINE_CONFIG_ROOT'] ?? '/config',
		platform,
		linkSupport: runtimeHints?.linkSupport ?? detectLinkSupport(environment),
		italicSupport:
			runtimeHints?.italicSupport ?? detectItalicSupport(environment),
		ssh,
	};
}
