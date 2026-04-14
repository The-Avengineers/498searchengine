import {
	existsSync,
	mkdirSync,
	readFileSync,
	renameSync,
	rmSync,
	writeFileSync,
} from 'node:fs';
import {homedir} from 'node:os';
import path from 'node:path';
import process from 'node:process';

import type {SshConfig} from './types.js';

export type PersistentCliConfig = {
	serverUrl?: string;
	ssh?: Partial<SshConfig>;
};

export type ConfigSetFlags = {
	server?: string;
	host?: string;
	sshHost?: string;
	sshPort?: number;
	sshUser?: string;
	sshKey?: string;
};

type ConfigCommandResult = {
	lines: string[];
	exitCode: number;
};

function platformConfigRoot(
	environment: NodeJS.ProcessEnv,
	platform: NodeJS.Platform,
) {
	if (platform === 'win32') {
		return environment['APPDATA'] ?? path.join(homedir(), 'AppData', 'Roaming');
	}

	return environment['XDG_CONFIG_HOME'] ?? path.join(homedir(), '.config');
}

export function getPersistentConfigPath(
	environment: NodeJS.ProcessEnv = process.env,
	platform: NodeJS.Platform = process.platform,
) {
	return path.join(
		platformConfigRoot(environment, platform),
		'avengine',
		'config.json',
	);
}

function readJsonConfig(configPath: string): PersistentCliConfig {
	const raw = readFileSync(configPath, 'utf8');
	const parsed = JSON.parse(raw) as unknown;

	if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
		throw new Error(`Invalid config at ${configPath}: expected a JSON object.`);
	}

	const record = parsed as Record<string, unknown>;
	const config: PersistentCliConfig = {};

	if (
		typeof record['serverUrl'] === 'string' &&
		record['serverUrl'].length > 0
	) {
		config.serverUrl = record['serverUrl'];
	}

	if (
		typeof record['ssh'] === 'object' &&
		record['ssh'] !== null &&
		!Array.isArray(record['ssh'])
	) {
		const sshRecord = record['ssh'] as Record<string, unknown>;
		const ssh: Partial<SshConfig> = {};
		if (typeof sshRecord['host'] === 'string' && sshRecord['host'].length > 0) {
			ssh.host = sshRecord['host'];
		}

		if (
			typeof sshRecord['port'] === 'number' &&
			Number.isFinite(sshRecord['port']) &&
			sshRecord['port'] > 0
		) {
			ssh.port = Math.floor(sshRecord['port']);
		}

		if (typeof sshRecord['user'] === 'string' && sshRecord['user'].length > 0) {
			ssh.user = sshRecord['user'];
		}

		if (
			typeof sshRecord['keyPath'] === 'string' &&
			sshRecord['keyPath'].length > 0
		) {
			ssh.keyPath = sshRecord['keyPath'];
		}

		if (Object.keys(ssh).length > 0) {
			config.ssh = ssh;
		}
	}

	return config;
}

export function readPersistentConfig(
	environment: NodeJS.ProcessEnv = process.env,
	platform: NodeJS.Platform = process.platform,
) {
	const configPath = getPersistentConfigPath(environment, platform);
	if (!existsSync(configPath)) {
		return {};
	}

	return readJsonConfig(configPath);
}

export function readPersistentConfigSafe(
	environment: NodeJS.ProcessEnv = process.env,
	platform: NodeJS.Platform = process.platform,
) {
	try {
		return readPersistentConfig(environment, platform);
	} catch {
		return {};
	}
}

function writePersistentConfig(
	config: PersistentCliConfig,
	environment: NodeJS.ProcessEnv = process.env,
	platform: NodeJS.Platform = process.platform,
) {
	const configPath = getPersistentConfigPath(environment, platform);
	mkdirSync(path.dirname(configPath), {recursive: true});
	writeFileSync(`${configPath}.tmp`, `${JSON.stringify(config, null, 2)}\n`, {
		mode: 0o600,
	});
	rmSync(configPath, {force: true});
	renameSync(`${configPath}.tmp`, configPath);
	return configPath;
}

function readExistingForCommand() {
	const configPath = getPersistentConfigPath();
	try {
		return {config: readPersistentConfig(), configPath};
	} catch (error: unknown) {
		const message =
			error instanceof Error ? error.message : 'Invalid config file.';
		return {error: message, configPath};
	}
}

function mergeConfig(
	current: PersistentCliConfig,
	flags: ConfigSetFlags,
): {config: PersistentCliConfig; changed: boolean} {
	const next: PersistentCliConfig = {
		...current,
		ssh: current.ssh ? {...current.ssh} : undefined,
	};
	let changed = false;

	if (flags.server) {
		next.serverUrl = flags.server;
		changed = true;
	}

	const sshHost = flags.sshHost ?? flags.host;
	const hasSshSettings = [
		sshHost,
		flags.sshPort,
		flags.sshUser,
		flags.sshKey,
	].some(value => value !== undefined && value !== '');
	if (hasSshSettings) {
		next.ssh = next.ssh ?? {};
	}

	if (sshHost) {
		next.ssh!.host = sshHost;
		changed = true;
	}

	if (flags.sshPort !== undefined) {
		if (!Number.isFinite(flags.sshPort) || flags.sshPort < 1) {
			throw new Error('--ssh-port must be a positive number.');
		}

		next.ssh!.port = Math.floor(flags.sshPort);
		changed = true;
	}

	if (flags.sshUser) {
		next.ssh!.user = flags.sshUser;
		changed = true;
	}

	if (flags.sshKey) {
		next.ssh!.keyPath = flags.sshKey;
		changed = true;
	}

	if (next.ssh && Object.keys(next.ssh).length === 0) {
		delete next.ssh;
	}

	return {config: next, changed};
}

function formatConfig(config: PersistentCliConfig, configPath: string) {
	const lines = [`config: ${configPath}`];
	lines.push(`server: ${config.serverUrl ?? '(not set)'}`);

	if (config.ssh?.host) {
		lines.push(
			`ssh host: ${config.ssh.host}`,
			`ssh port: ${config.ssh.port ?? 2222}`,
			`ssh user: ${config.ssh.user ?? 'engine'}`,
			`ssh key: ${config.ssh.keyPath ?? '(default ssh agent/key)'}`,
		);
	} else {
		lines.push('ssh: (not set)');
	}

	return lines;
}

export function runConfigCommand(
	args: string[],
	flags: ConfigSetFlags,
): ConfigCommandResult {
	const subcommand = args[0] ?? 'show';

	if (subcommand === 'show') {
		const result = readExistingForCommand();
		if (result.error) {
			return {lines: [result.error], exitCode: 1};
		}

		return {
			lines: formatConfig(result.config ?? {}, result.configPath),
			exitCode: 0,
		};
	}

	if (subcommand === 'clear') {
		const configPath = getPersistentConfigPath();
		rmSync(configPath, {force: true});
		return {lines: [`cleared config: ${configPath}`], exitCode: 0};
	}

	if (subcommand === 'set') {
		const result = readExistingForCommand();
		if (result.error) {
			return {lines: [result.error], exitCode: 1};
		}

		try {
			const {config, changed} = mergeConfig(result.config ?? {}, flags);
			if (!changed) {
				return {
					lines: [
						'No config values provided.',
						'Use --server, --ssh-host, --ssh-port, --ssh-user, or --ssh-key.',
					],
					exitCode: 1,
				};
			}

			const configPath = writePersistentConfig(config);
			return {
				lines: ['saved AVEngine config', ...formatConfig(config, configPath)],
				exitCode: 0,
			};
		} catch (error: unknown) {
			return {
				lines: [
					error instanceof Error ? error.message : 'Could not save config.',
				],
				exitCode: 1,
			};
		}
	}

	return {
		lines: ['Config command must be one of: show, set, clear.'],
		exitCode: 1,
	};
}
