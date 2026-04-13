import {execFile, spawn} from 'node:child_process';
import type {ChildProcess} from 'node:child_process';

import type {SshConfig} from './types.js';

function buildSshArguments(ssh: SshConfig, remoteCommand: string): string[] {
	const arguments_: string[] = [
		'-o', 'BatchMode=yes',
		'-o', 'StrictHostKeyChecking=no',
		'-o', 'ConnectTimeout=10',
		'-p', String(ssh.port),
	];

	if (ssh.keyPath) {
		arguments_.push('-i', ssh.keyPath);
	}

	arguments_.push(`${ssh.user}@${ssh.host}`, remoteCommand);
	return arguments_;
}

export async function executeRemoteScript(
	remotePath: string,
	ssh: SshConfig,
	arguments_: string[] = [],
): Promise<string> {
	// Build a single shell command string so args are properly quoted on the remote
	const remoteCommand = [remotePath, ...arguments_.map(a => `'${a}'`)].join(' ');
	const sshArguments = buildSshArguments(ssh, remoteCommand);

	return new Promise((resolve, reject) => {
		execFile('ssh', sshArguments, (error, stdout, stderr) => {
			if (error) {
				const detail =
					stderr.trim().length > 0 ? stderr.trim() : error.message;

				// Surface helpful config errors
				if (detail.includes('Permission denied')) {
					reject(
						new Error(
							`SSH authentication failed for ${ssh.user}@${ssh.host}:${ssh.port}. Check AVENGINE_SSH_KEY.`,
						),
					);
					return;
				}

				if (
					detail.includes('Connection refused') ||
					detail.includes('No route to host') ||
					detail.includes('Connection timed out')
				) {
					reject(
						new Error(
							`SSH connection to ${ssh.host}:${ssh.port} failed: ${detail}`,
						),
					);
					return;
				}

				reject(new Error(detail));
				return;
			}

			resolve(stdout.trim());
		});
	});
}

// Creates a persistent SSH local-port-forward tunnel.
// The returned ChildProcess IS the tunnel — kill it to close the tunnel.
// localPort:localhost:remotePort on the container side.
export function createSshTunnel(
	localPort: number,
	remotePort: number,
	ssh: SshConfig,
): ChildProcess {
	const arguments_: string[] = [
		'-N',
		'-o', 'BatchMode=yes',
		'-o', 'StrictHostKeyChecking=no',
		'-o', 'ConnectTimeout=10',
		'-o', 'ServerAliveInterval=10',
		'-o', 'ServerAliveCountMax=3',
		'-p', String(ssh.port),
		'-L', `${localPort}:localhost:${remotePort}`,
	];

	if (ssh.keyPath) {
		arguments_.push('-i', ssh.keyPath);
	}

	arguments_.push(`${ssh.user}@${ssh.host}`);

	return spawn('ssh', arguments_, {stdio: 'pipe'});
}

export async function testSshConnectivity(
	ssh: SshConfig,
): Promise<{ok: boolean; error?: string}> {
	try {
		const result = await executeRemoteScript('echo', ssh, ['ok']);
		return {ok: result.trim() === 'ok'};
	} catch (error: unknown) {
		return {
			ok: false,
			error: error instanceof Error ? error.message : 'SSH connection failed.',
		};
	}
}
