import http from 'node:http';
import https from 'node:https';

import type {CliConfig, SearchRequest, SearchResponse} from './types.js';

function readNumber(value: unknown, fallback = 0) {
	return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

function readString(value: unknown, fallback = '') {
	return typeof value === 'string' ? value : fallback;
}

function readSearchErrors(rawErrors: unknown) {
	if (!Array.isArray(rawErrors)) {
		return [];
	}

	return rawErrors
		.map(error => {
			if (typeof error !== 'object' || error === null) {
				return undefined;
			}

			return {
				offset: readNumber((error as Record<string, unknown>)['offset']),
				message: readString((error as Record<string, unknown>)['message']),
			};
		})
		.filter((error): error is {offset: number; message: string} => {
			return error !== undefined && error.message.length > 0;
		});
}

function readSearchResults(rawResults: unknown) {
	if (!Array.isArray(rawResults)) {
		return [];
	}

	return rawResults
		.map(result => {
			if (typeof result !== 'object' || result === null) {
				return undefined;
			}

			const record = result as Record<string, unknown>;
			return {
				url: readString(record['url']),
				title: readString(
					record['title'],
					readString(record['url'], 'Untitled result'),
				),
				description: readString(record['description']),
				score: readNumber(record['score']),
			};
		})
		.filter(
			(result): result is {
				url: string;
				title: string;
				description: string;
				score: number;
			} =>
				result !== undefined && result.url.length > 0,
		);
}

function readResponseBody(bodyText: string) {
	if (bodyText.length === 0) {
		return {};
	}

	try {
		return JSON.parse(bodyText) as Record<string, unknown>;
	} catch {
		throw new Error('API returned invalid JSON.');
	}
}

function normalizeResponse(
	request: SearchRequest,
	payload: Record<string, unknown>,
): SearchResponse {
	return {
		query: readString(payload['query'], request.query),
		page: readNumber(payload['page'], request.page),
		limit: readNumber(payload['limit'], request.limit),
		totalHits: readNumber(payload['total_hits']),
		tookMs: readNumber(payload['took_ms']),
		parseMs: readNumber(payload['parse_ms']),
		solveMs: readNumber(payload['solve_ms']),
		collectMs: readNumber(payload['collect_ms']),
		rankMs: readNumber(payload['rank_ms']),
		errors: readSearchErrors(payload['errors']),
		results: readSearchResults(payload['results']),
	};
}

export async function requestSearch(
	config: CliConfig,
	request: SearchRequest,
): Promise<SearchResponse> {
	const searchUrl = new URL('/api/search', config.serverUrl);
	searchUrl.searchParams.set('q', request.query);
	searchUrl.searchParams.set('page', String(request.page));
	searchUrl.searchParams.set('limit', String(request.limit));

	const transport = searchUrl.protocol === 'https:' ? https : http;
	const payload = await new Promise<Record<string, unknown>>(
		(resolve, reject) => {
			const incomingRequest = transport.get(
				searchUrl,
				{
					headers: {
						Accept: 'application/json',
					},
				},
				response => {
					const chunks: string[] = [];
					response.setEncoding('utf8');
					response.on('data', chunk => {
						chunks.push(chunk);
					});
					response.on('end', () => {
						try {
							const bodyText = chunks.join('');
							const body = readResponseBody(bodyText);
							if (
								response.statusCode !== undefined &&
								(response.statusCode < 200 || response.statusCode >= 300)
							) {
								const errors = readSearchErrors(body['errors']);
								const message =
									errors[0]?.message ??
									`API request failed (${response.statusCode}).`;
								reject(new Error(message));
								return;
							}

							resolve(body);
						} catch (error: unknown) {
							reject(
								error instanceof Error
									? error
									: new Error('Unknown API response failure.'),
							);
						}
					});
				},
			);

			incomingRequest.on('error', error => {
				reject(new Error(`Search request failed: ${error.message}`));
			});
		},
	);

	return normalizeResponse(request, payload);
}
