import CFonts from 'cfonts';

import {formatTerminalLink} from './links.js';
import type {
	LuckyOpenResult,
	SearchDisplayMode,
	SearchResponse,
	SystemStatusData,
} from './types.js';

const TIMING_BAR_WIDTH = 20;

function pluralizeResults(count: number) {
	return count === 1 ? 'result' : 'results';
}

function formatScore(score: number) {
	const formatted = score.toFixed(6).replace(/0+$/u, '').replace(/\.$/u, '');
	return formatted.length > 0 ? formatted : '0';
}

function getResultLinkLabel(title: string, url: string) {
	const trimmedTitle = title.trim();
	return trimmedTitle.length > 0 ? trimmedTitle : url;
}

function scaleBannerLine(line: string, scale: number) {
	const glyphs = [...line.replace(/\s+$/u, '')];
	if (glyphs.length === 0 || scale <= 1) {
		return line.replace(/\s+$/u, '');
	}

	const scaledWidth = Math.max(
		glyphs.length,
		Math.round(glyphs.length * scale),
	);
	let scaledLine = '';
	for (let index = 0; index < scaledWidth; index += 1) {
		const sourceIndex = Math.min(glyphs.length - 1, Math.floor(index / scale));
		scaledLine += glyphs[sourceIndex]!;
	}

	return scaledLine;
}

function scaleBannerLines(lines: string[], scale: number) {
	if (scale <= 1 || lines.length === 0) {
		return lines;
	}

	const scaledHeight = Math.max(lines.length, Math.round(lines.length * scale));
	return Array.from({length: scaledHeight}, (_, index: number) => {
		const sourceIndex = Math.min(lines.length - 1, Math.floor(index / scale));
		return scaleBannerLine(lines[sourceIndex]!, scale);
	});
}

export function getBannerLines() {
	const bannerScale = 1.5;
	const banner = CFonts.render('AVENGINE', {
		font: 'block',
		align: 'left',
		colors: ['system'],
		backgroundColor: 'transparent',
		letterSpacing: 0,
		lineHeight: 1,
		space: true,
		maxLength: 0,
	});
	if (banner === false) {
		return ['AVENGINE'];
	}

	const lines = banner.string
		.split('\n')
		.filter((line: string) => line.trim().length > 0);

	const scaledLines = scaleBannerLines(lines, bannerScale);

	return scaledLines.map((line: string, index: number, allLines: string[]) => {
		const offset = Math.max(0, allLines.length - index - 1);
		return `${' '.repeat(offset)}${line}`;
	});
}

export function getCompactHeader(mode: SearchDisplayMode) {
	return `AVENGINE :: ${mode.toUpperCase()} MODE`;
}

export function summarizeSearch(response: SearchResponse) {
	return `${response.totalHits} ${pluralizeResults(response.totalHits)} for "${
		response.query
	}" in ${response.tookMs} ms.`;
}

export function formatTimingLine(response: SearchResponse) {
	return `timings: took ${response.tookMs} ms | parse ${response.parseMs} ms | solve ${response.solveMs} ms | collect ${response.collectMs} ms | rank ${response.rankMs} ms`;
}

function buildTimingBar(phaseMs: number, totalMs: number): string {
	if (totalMs === 0) return '░'.repeat(TIMING_BAR_WIDTH);
	const filled = Math.min(
		TIMING_BAR_WIDTH,
		Math.round((phaseMs / totalMs) * TIMING_BAR_WIDTH),
	);
	return '█'.repeat(filled) + '░'.repeat(TIMING_BAR_WIDTH - filled);
}

export function formatTimingBar(response: SearchResponse): string[] {
	const total = response.tookMs;
	const phases: Array<{label: string; ms: number}> = [
		{label: 'parse', ms: response.parseMs},
		{label: 'solve', ms: response.solveMs},
		{label: 'collect', ms: response.collectMs},
		{label: 'rank', ms: response.rankMs},
	];
	const labelWidth = Math.max(...phases.map(p => p.label.length));
	const msWidth = Math.max(...phases.map(p => String(p.ms).length));

	return phases.map(({label, ms}) => {
		const paddedLabel = label.padEnd(labelWidth);
		const bar = buildTimingBar(ms, total);
		const paddedMs = String(ms).padStart(msWidth);
		return `${paddedLabel}  ${bar}  ${paddedMs}ms`;
	});
}

export function formatSearchLines(
	response: SearchResponse,
	mode: SearchDisplayMode,
	linkSupport: boolean,
) {
	const lines = [summarizeSearch(response)];

	for (const error of response.errors) {
		lines.push(`warning: ${error.message}`);
	}

	if (mode === 'verbose') {
		lines.push(formatTimingLine(response), ...formatTimingBar(response));
	}

	if (response.results.length === 0) {
		lines.push(`No results found for "${response.query}".`);
		return lines;
	}

	for (const [index, result] of response.results.entries()) {
		const rank = `${index + 1}.`;
		const linkLabel = getResultLinkLabel(result.title, result.url);
		const link = formatTerminalLink(linkLabel, result.url, linkSupport);
		if (mode === 'query') {
			lines.push(`${rank} ${link}`);
			continue;
		}

		lines.push(
			`${rank} ${link}`,
			`   ${result.url}`,
			...(result.description.trim().length > 0
				? [`   ${result.description}`]
				: []),
			`   score ${formatScore(result.score)}`,
		);
	}

	return lines;
}

export function formatLuckyLines(
	response: SearchResponse,
	linkSupport: boolean,
	openResult: LuckyOpenResult,
) {
	const lines = [summarizeSearch(response)];

	for (const error of response.errors) {
		lines.push(`warning: ${error.message}`);
	}

	if (response.results.length === 0) {
		lines.push(
			`No result available for "${response.query}".`,
			openResult.detail,
		);
		return lines;
	}

	const top = response.results[0]!;
	const linkLabel = getResultLinkLabel(top.title, top.url);
	lines.push(
		`Lucky hit: ${formatTerminalLink(linkLabel, top.url, linkSupport)}`,
		`url: ${top.url}`,
		...(top.description.trim().length > 0
			? [`description: ${top.description}`]
			: []),
		`score: ${formatScore(top.score)}`,
		openResult.detail,
	);
	return lines;
}

export function formatSystemStatusLines(status: SystemStatusData): string[] {
	const serverStatus = status.searchServerOk
		? `ok${status.searchServerLatencyMs !== undefined ? ` (${status.searchServerLatencyMs}ms)` : ''}`
		: 'unreachable';

	return [
		'system status:',
		`  search server:  ${serverStatus}`,
		`  crawler swarm:  ${status.crawlerSwarm}`,
		`  data usage:     ${status.dataUsage}`,
		`  corpus shards:  ${status.corpusShards}`,
	];
}

export function formatCrawlerStubLines(runtimeBinRoot: string) {
	return [
		'Crawler mode is not available in this environment yet.',
		`Expected AVENGINE runtime scripts under ${runtimeBinRoot}.`,
		'Set AVENGINE_RUNTIME_BIN_ROOT to a managed AVENGINE runtime bin directory to enable crawler control.',
		'Set AVENGINE_SSH_HOST to connect to a remote deploy container.',
	];
}

export function getWelcomeLines() {
	return [
		'Press i to type a query, Enter to run it, and Esc to return to normal mode.',
		'Quit from normal mode with q, or use Ctrl+C anytime.',
		'Use :query, :verbose, :lucky, :stats, :status, :open, or :crawler status|start|stop|index|watch from command mode.',
		'Pagination: :next, :prev, :page N after a search.',
	];
}

export function getHelpLines() {
	return [
		'AVENGINE help',
		'',
		'normal mode:',
		'  i        enter insert mode (type query)',
		'  :        enter command mode',
		'  ?        toggle help',
		'  q        quit',
		'  Ctrl+C   force quit',
		'',
		'insert mode:',
		'  type     query text',
		'  Enter    run search',
		'  Esc      return to normal mode',
		'',
		'command mode:',
		'  :query [text]   | :verbose [text]  — search',
		'  :lucky [text]                       — open first result in browser',
		'  :next | :prev | :page N             — paginate results',
		'  :stats                              — live crawler stats (q to return)',
		'  :status                             — system status snapshot',
		'  :open                               — open configured AVENGINE site in browser',
		'  :crawler status|start|stop|index|watch',
		'  :help | :quit',
		'',
		'one-shot commands:',
		'  avengine query "apple" [--verbose]',
		'  avengine lucky "apple"',
		'  avengine crawler status|start|stop|index|watch|profile',
		'  avengine stats [--once]',
		'  avengine status',
		'  avengine profile',
		'  avengine open [--port 8080]         — open configured AVENGINE site in browser',
		'',
		'remote access (set AVENGINE_SSH_HOST):',
		'  AVENGINE_SSH_HOST=host avengine stats',
		'  AVENGINE_SERVER_URL=https://theavengine.com avengine open',
		'  AVENGINE_SSH_HOST=host avengine query "apple"',
		'  env: AVENGINE_SSH_PORT (default 2222) | AVENGINE_SSH_USER (default engine) | AVENGINE_SSH_KEY',
	];
}
