import React, {useCallback, useEffect, useRef, useState} from 'react';
import {Box, Text, useApp, useInput} from 'ink';

import {fetchRawStats} from '../lib/crawler.js';
import {
	buildSnapshot,
	parseStatsOutput,
	runningWorkerCount,
	totalCrawlRows,
	totalDocsPerSec,
} from '../lib/stats.js';
import {requestSearch} from '../lib/api.js';
import type {
	CliConfig,
	CrawlerStats,
	StatsSnapshot,
	WorkerStats,
} from '../lib/types.js';

const REFRESH_INTERVAL_MS = 2000;

type Props = {
	config: CliConfig;
	once: boolean;
	onExit?: () => void;
};

type ServerStatus =
	| {state: 'unknown'}
	| {state: 'ok'; latencyMs: number}
	| {state: 'error'; message: string};

function stateColor(state: WorkerStats['state']): string {
	if (state === 'running') return 'green';
	if (state === 'stale-pid') return 'yellow';
	return 'red';
}

function formatDocsPerSec(value?: number): string {
	if (value === undefined) return '  —  ';
	if (value < 0.1) return ' 0.0 ';
	return value.toFixed(1).padStart(5);
}

function formatCount(n: number): string {
	return n.toLocaleString('en-US');
}

function WorkerRow({worker}: {readonly worker: WorkerStats}) {
	return (
		<Box>
			<Text>{worker.workerId.padEnd(8)}</Text>
			<Text color={stateColor(worker.state)}>{worker.state.padEnd(12)}</Text>
			<Text>{formatCount(worker.crawlRows).padStart(10)}  </Text>
			<Text>{formatDocsPerSec(worker.docsPerSec).padStart(8)}/s  </Text>
			<Text>{String(worker.sfiFiles).padStart(6)}  </Text>
			<Text>{worker.checkpoint ? 'yes' : 'no'}</Text>
		</Box>
	);
}

function ServerStatusLine({
	serverStatus,
}: {
	readonly serverStatus: ServerStatus;
}) {
	if (serverStatus.state === 'unknown') {
		return <Text dimColor>search server: checking...</Text>;
	}

	if (serverStatus.state === 'ok') {
		return (
			<Text>
				search server:{' '}
				<Text color="green">ok</Text>
				{` (${serverStatus.latencyMs}ms)`}
			</Text>
		);
	}

	return (
		<Text>
			search server: <Text color="red">unreachable</Text>
			{` — ${serverStatus.message}`}
		</Text>
	);
}

export default function StatsDashboard({config, once, onExit}: Readonly<Props>) {
	const {exit} = useApp();
	const [stats, setStats] = useState<CrawlerStats | null>(null);
	const [serverStatus, setServerStatus] = useState<ServerStatus>({
		state: 'unknown',
	});
	const [lastRefresh, setLastRefresh] = useState<Date | null>(null);
	const [loadError, setLoadError] = useState<string | null>(null);
	const snapshotRef = useRef<StatsSnapshot | undefined>(undefined);

	const handleExit = useCallback(() => {
		if (onExit) {
			onExit();
		} else {
			exit();
		}
	}, [onExit, exit]);

	useInput((input, key) => {
		if (input === 'q' || (key?.ctrl && input === 'c')) {
			handleExit();
		}
	});

	const refresh = useCallback(async () => {
		try {
			const rawStats = await fetchRawStats(config);
			const parsed = parseStatsOutput(rawStats, snapshotRef.current);
			snapshotRef.current = buildSnapshot(parsed);
			setStats(parsed);
			setLastRefresh(new Date());
			setLoadError(null);
		} catch (error: unknown) {
			setLoadError(
				error instanceof Error ? error.message : 'Failed to fetch stats.',
			);
		}
	}, [config]);

	const refreshServer = useCallback(async () => {
		const start = Date.now();
		try {
			await requestSearch(config, {query: 'ping', page: 1, limit: 1});
			setServerStatus({state: 'ok', latencyMs: Date.now() - start});
		} catch {
			const latencyMs = Date.now() - start;
			if (latencyMs < 5000) {
				// Got a response (maybe a query error), server is up
				setServerStatus({state: 'ok', latencyMs});
			} else {
				setServerStatus({state: 'error', message: 'timeout'});
			}
		}
	}, [config]);

	useEffect(() => {
		void refresh();
		void refreshServer();

		if (once) return;

		const statsInterval = setInterval(() => {
			void refresh();
		}, REFRESH_INTERVAL_MS);

		const serverInterval = setInterval(() => {
			void refreshServer();
		}, 10_000);

		return () => {
			clearInterval(statsInterval);
			clearInterval(serverInterval);
		};
	}, [once, refresh, refreshServer]);

	// In once-mode, exit after first data load
	useEffect(() => {
		if (once && stats !== null) {
			// Small delay so the render is visible
			const timer = setTimeout(handleExit, 200);
			return () => {
				clearTimeout(timer);
			};
		}

		return undefined;
	}, [once, stats, handleExit]);

	const timestamp = lastRefresh
		? lastRefresh.toLocaleTimeString()
		: 'loading...';

	const separator = '─'.repeat(60);

	return (
		<Box flexDirection="column">
			<Text bold color="cyanBright">
				{'AVENGINE STATS :: '}
				{timestamp}
				{config.ssh ? ` [remote: ${config.ssh.host}]` : ''}
			</Text>

			<Box marginTop={1}>
				<Text bold>{'WORKER  '}</Text>
				<Text bold>{'STATE        '}</Text>
				<Text bold>{'      DOCS  '}</Text>
				<Text bold>{'  DOCS/S  '}</Text>
				<Text bold>{'SHARDS  '}</Text>
				<Text bold>CKPT</Text>
			</Box>

			{loadError ? (
				<Text color="red">{loadError}</Text>
			) : stats === null ? (
				<Text dimColor>Loading...</Text>
			) : (
				<>
					{stats.workers.length === 0 ? (
						<Text dimColor>No crawler workers found.</Text>
					) : (
						stats.workers.map(worker => (
							<WorkerRow key={worker.workerId} worker={worker} />
						))
					)}

					<Text dimColor>{separator}</Text>

					<Box>
						<Text>
							{'total  '}
							<Text bold>{formatCount(totalCrawlRows(stats.workers))}</Text>
							{' docs   '}
							<Text bold>{totalDocsPerSec(stats.workers).toFixed(1)}</Text>
							{'/s   '}
							<Text dimColor>corpus: {stats.currentCorpusShards} shards</Text>
							{'   '}
							<Text dimColor>
								workers: {runningWorkerCount(stats.workers)}/
								{stats.workers.length} running
							</Text>
						</Text>
					</Box>
				</>
			)}

			<Box marginTop={1}>
				<ServerStatusLine serverStatus={serverStatus} />
			</Box>

			<Text dimColor>
				{once ? 'one-shot' : `refreshing every ${REFRESH_INTERVAL_MS / 1000}s`}
				{' | q to '}
				{onExit ? 'return to search' : 'exit'}
			</Text>
		</Box>
	);
}
