import React, {useEffect, useState} from 'react';
import {Box, Text, useApp} from 'ink';

import {aggregateSystemStatus} from '../lib/status.js';
import type {CliConfig, SystemStatusData} from '../lib/types.js';

type Props = {
	config: CliConfig;
};

export default function SystemStatus({config}: Readonly<Props>) {
	const {exit} = useApp();
	const [status, setStatus] = useState<SystemStatusData | null>(null);
	const [error, setError] = useState<string | null>(null);

	useEffect(() => {
		aggregateSystemStatus(config)
			.then(result => {
				setStatus(result);
			})
			.catch((err: unknown) => {
				setError(
					err instanceof Error ? err.message : 'Failed to load status.',
				);
			});
	}, [config]);

	useEffect(() => {
		if (status !== null || error !== null) {
			// Brief pause so the render is visible before exiting
			const timer = setTimeout(exit, 100);
			return () => {
				clearTimeout(timer);
			};
		}

		return undefined;
	}, [status, error, exit]);

	if (error) {
		return (
			<Box flexDirection="column">
				<Text bold color="cyanBright">
					AVENGINE STATUS
					{config.ssh ? ` [remote: ${config.ssh.host}]` : ''}
				</Text>
				<Text color="red">{error}</Text>
			</Box>
		);
	}

	if (!status) {
		return (
			<Box>
				<Text dimColor>Checking system status...</Text>
			</Box>
		);
	}

	const serverColor = status.searchServerOk ? 'green' : 'red';
	const serverText = status.searchServerOk
		? `ok${status.searchServerLatencyMs !== undefined ? ` (${status.searchServerLatencyMs}ms)` : ''}`
		: 'unreachable';

	return (
		<Box flexDirection="column">
			<Text bold color="cyanBright">
				AVENGINE STATUS
				{config.ssh ? ` [remote: ${config.ssh.host}]` : ''}
			</Text>

			<Box marginTop={1} flexDirection="column">
				<Box>
					<Text>{'search server:  '}</Text>
					<Text color={serverColor}>{serverText}</Text>
				</Box>
				<Box>
					<Text>{'crawler swarm:  '}</Text>
					<Text>{status.crawlerSwarm}</Text>
				</Box>
				<Box>
					<Text>{'data usage:     '}</Text>
					<Text>{status.dataUsage}</Text>
				</Box>
				<Box>
					<Text>{'corpus shards:  '}</Text>
					<Text>{status.corpusShards}</Text>
				</Box>
			</Box>
		</Box>
	);
}
