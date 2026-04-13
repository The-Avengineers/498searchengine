import {requestSearch} from './api.js';
import {fetchRawStatus} from './crawler.js';
import type {CliConfig, SystemStatusData} from './types.js';

function parseDataUsage(statusOutput: string): string {
	for (const line of statusOutput.split('\n')) {
		const trimmed = line.trim();
		if (trimmed.startsWith('data usage:')) {
			return trimmed.slice('data usage:'.length).trim();
		}
	}

	return 'unknown';
}

function parseCrawlerSwarm(statusOutput: string): string {
	for (const line of statusOutput.split('\n')) {
		const trimmed = line.trim();
		if (trimmed.startsWith('crawler swarm:')) {
			return trimmed.slice('crawler swarm:'.length).trim();
		}
	}

	return 'unknown';
}

function parseCorpusShards(statusOutput: string): number {
	for (const line of statusOutput.split('\n')) {
		const trimmed = line.trim();
		if (trimmed.startsWith('current corpus shards:')) {
			const value = trimmed.slice('current corpus shards:'.length).trim();
			const parsed = Number.parseInt(value, 10);
			return Number.isFinite(parsed) ? parsed : 0;
		}
	}

	return 0;
}

async function pingSearchServer(
	config: CliConfig,
): Promise<{ok: boolean; latencyMs?: number}> {
	const start = Date.now();
	try {
		await requestSearch(config, {query: 'status', page: 1, limit: 1});
		return {ok: true, latencyMs: Date.now() - start};
	} catch {
		// A query error (e.g. no results) is still a successful connection
		const latencyMs = Date.now() - start;
		if (latencyMs < 5000) {
			return {ok: true, latencyMs};
		}

		return {ok: false};
	}
}

export async function aggregateSystemStatus(
	config: CliConfig,
): Promise<SystemStatusData> {
	const [pingResult, statusOutput] = await Promise.all([
		pingSearchServer(config),
		fetchRawStatus(config).catch(() => ''),
	]);

	return {
		searchServerOk: pingResult.ok,
		searchServerLatencyMs: pingResult.latencyMs,
		crawlerSwarm: parseCrawlerSwarm(statusOutput),
		dataUsage: parseDataUsage(statusOutput),
		corpusShards: parseCorpusShards(statusOutput),
	};
}
