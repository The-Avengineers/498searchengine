import CFonts from 'cfonts';

import {formatTerminalLink} from './links.js';
import type {
	LuckyOpenResult,
	SearchDisplayMode,
	SearchResponse,
} from './types.js';

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
		lines.push(formatTimingLine(response));
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
		`score: ${formatScore(top.score)}`,
		openResult.detail,
	);
	return lines;
}

export function formatCrawlerStubLines(runtimeBinRoot: string) {
	return [
		'Crawler mode is not available in this environment yet.',
		`Expected AVENGINE runtime scripts under ${runtimeBinRoot}.`,
		'Set AVENGINE_RUNTIME_BIN_ROOT to a managed AVENGINE runtime bin directory to enable crawler control.',
	];
}

export function getWelcomeLines() {
	return [
		'Press i to type a query, Enter to run it, and Esc to return to normal mode.',
		'Quit from normal mode with q, or use Ctrl+C anytime.',
		'Use :query, :verbose, :lucky, or :crawler status|start|stop|index from command mode.',
		'Offline game: coming soon.',
	];
}

export function getHelpLines() {
	return [
		'AVENGINE help',
		'normal mode: i insert | : command | ? help | q quit | Ctrl+C force quit',
		'insert mode: type query text | Enter run current search mode | Esc return to normal, then q quits',
		'command mode: :query [text] | :verbose [text] | :lucky [text] | :crawler status|start|stop|index | :help | :quit | q quits on an empty command line',
		'one-shot: avengine query "apple" --verbose | avengine lucky "apple" | avengine crawler status',
		'offline game: coming soon',
	];
}
