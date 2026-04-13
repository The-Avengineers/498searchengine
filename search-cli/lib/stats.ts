import type {CrawlerStats, StatsSnapshot, WorkerStats} from './types.js';

type WorkerState = 'running' | 'stopped' | 'stale-pid';

function parseWorkerState(raw: string): WorkerState {
	if (raw === 'running') return 'running';
	if (raw === 'stale-pid') return 'stale-pid';
	return 'stopped';
}

// Parse the columnar output of avengine-stats.sh:
// worker   state    sfi_files  crawl_rows  ac_log_mb  checkpoint
// 0        running  12         184231      14.0       yes
export function parseStatsOutput(
	output: string,
	previousSnapshot?: StatsSnapshot,
): CrawlerStats {
	const now = Date.now();
	const workers: WorkerStats[] = [];
	let totalWorkerShards = 0;
	let currentCorpusShards = 0;

	for (const line of output.split('\n')) {
		const trimmed = line.trim();

		// Footer lines like "total worker shards: N"
		const totalShardsMatch = /^total worker shards:\s+(\d+)$/u.exec(trimmed);
		if (totalShardsMatch) {
			totalWorkerShards = Number.parseInt(totalShardsMatch[1]!, 10);
			continue;
		}

		const corpusShardsMatch = /^current corpus shards:\s+(\d+)$/u.exec(trimmed);
		if (corpusShardsMatch) {
			currentCorpusShards = Number.parseInt(corpusShardsMatch[1]!, 10);
			continue;
		}

		// Skip header row and blank lines
		if (
			trimmed.length === 0 ||
			trimmed.startsWith('worker') ||
			trimmed.startsWith('#')
		) {
			continue;
		}

		// Parse data row: columns are whitespace-separated
		const columns = trimmed.split(/\s+/u);
		if (columns.length < 6) continue;

		const [workerId, stateRaw, sfiFilesRaw, crawlRowsRaw, acLogMbRaw, checkpointRaw] =
			columns as [string, string, string, string, string, string];

		const crawlRows = Number.parseInt(crawlRowsRaw, 10);

		let docsPerSec: number | undefined;
		if (previousSnapshot) {
			const previousRows = previousSnapshot.rowsByWorker[workerId] ?? 0;
			const elapsedSeconds = Math.max(
				0.001,
				(now - previousSnapshot.timestamp) / 1000,
			);
			const delta = Math.max(0, crawlRows - previousRows);
			docsPerSec = delta / elapsedSeconds;
		}

		workers.push({
			workerId: workerId!,
			state: parseWorkerState(stateRaw!),
			sfiFiles: Number.parseInt(sfiFilesRaw, 10),
			crawlRows,
			acLogMb: Number.parseFloat(acLogMbRaw),
			checkpoint: checkpointRaw === 'yes',
			docsPerSec,
		});
	}

	return {workers, totalWorkerShards, currentCorpusShards};
}

export function buildSnapshot(stats: CrawlerStats): StatsSnapshot {
	const rowsByWorker: Record<string, number> = {};
	for (const worker of stats.workers) {
		rowsByWorker[worker.workerId] = worker.crawlRows;
	}

	return {timestamp: Date.now(), rowsByWorker};
}

export function totalDocsPerSec(workers: WorkerStats[]): number {
	return workers.reduce((sum, w) => sum + (w.docsPerSec ?? 0), 0);
}

export function totalCrawlRows(workers: WorkerStats[]): number {
	return workers.reduce((sum, w) => sum + w.crawlRows, 0);
}

export function runningWorkerCount(workers: WorkerStats[]): number {
	return workers.filter(w => w.state === 'running').length;
}
