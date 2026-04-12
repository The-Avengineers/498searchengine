import {execFile} from 'node:child_process';
import {existsSync} from 'node:fs';
import process from 'node:process';

import {formatCrawlerStubLines} from './presentation.js';
import type {CliConfig, CrawlerCommand, CrawlerCommandResult} from './types.js';

async function executeScript(
	scriptPath: string,
	config: CliConfig,
	arguments_: string[] = [],
): Promise<string> {
	return new Promise((resolve, reject) => {
		execFile(
			scriptPath,
			arguments_,
			{
				env: {
					...process.env,
					AVENGINE_BIN_ROOT: config.runtimeBinRoot,
					AVENGINE_DATA_ROOT: config.dataRoot,
					AVENGINE_CONFIG_ROOT: config.configRoot,
				},
			},
			(error, stdout, stderr) => {
				if (error) {
					const detail =
						stderr.trim().length > 0 ? stderr.trim() : error.message;
					reject(new Error(detail));
					return;
				}

				resolve(stdout.trim());
			},
		);
	});
}

function resolveScriptPath(config: CliConfig, fileName: string) {
	return `${config.runtimeBinRoot}/${fileName}`;
}

async function readStatusLines(config: CliConfig) {
	const statusOutput = await executeScript(
		resolveScriptPath(config, 'avengine-status.sh'),
		config,
	);
	const statsOutput = await executeScript(
		resolveScriptPath(config, 'avengine-stats.sh'),
		config,
	);

	return {
		statusOutput,
		lines: [
			...statusOutput.split('\n').filter(line => line.length > 0),
			'',
			...statsOutput.split('\n').filter(line => line.length > 0),
		],
	};
}

export function extractIndexLines(statusOutput: string) {
	return statusOutput
		.split('\n')
		.map(line => line.trim())
		.filter(line => {
			return (
				line.startsWith('data usage:') ||
				line.startsWith('aggregate limit:') ||
				line.startsWith('current corpus shards:') ||
				line.startsWith('promoted profile shards:')
			);
		});
}

function hasRuntimeSupport(config: CliConfig) {
	const requiredScripts = [
		resolveScriptPath(config, 'avengine-status.sh'),
		resolveScriptPath(config, 'avengine-stats.sh'),
		resolveScriptPath(config, 'avengine-start.sh'),
		resolveScriptPath(config, 'avengine-stop.sh'),
	];

	return requiredScripts.every(scriptPath => {
		return existsSync(scriptPath);
	});
}

export async function runCrawlerCommand(
	config: CliConfig,
	command: CrawlerCommand,
): Promise<CrawlerCommandResult> {
	if (!hasRuntimeSupport(config)) {
		return {
			status: 'Crawler runtime unavailable.',
			lines: formatCrawlerStubLines(config.runtimeBinRoot),
			exitCode: 0,
		};
	}

	if (command === 'status') {
		const {lines} = await readStatusLines(config);
		return {
			status: 'Crawler status loaded.',
			lines,
			exitCode: 0,
		};
	}

	if (command === 'index') {
		const {statusOutput} = await readStatusLines(config);
		return {
			status: 'Crawler index status loaded.',
			lines: extractIndexLines(statusOutput),
			exitCode: 0,
		};
	}

	if (command === 'start') {
		const output = await executeScript(
			resolveScriptPath(config, 'avengine-start.sh'),
			config,
		);
		return {
			status: 'Crawler start requested.',
			lines:
				output.length > 0 ? [output] : ['started managed AVEngine services'],
			exitCode: 0,
		};
	}

	const output = await executeScript(
		resolveScriptPath(config, 'avengine-stop.sh'),
		config,
	);
	return {
		status: 'Crawler stop requested.',
		lines: output.length > 0 ? [output] : ['stopped managed Avengine services'],
		exitCode: 0,
	};
}
