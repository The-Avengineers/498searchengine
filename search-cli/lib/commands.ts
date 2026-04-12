import type {
	CliInvocation,
	CommandAction,
	InteractiveInvocation,
	SearchDisplayMode,
} from './types.js';

type InvocationOptions = {
	server?: string;
	page?: number;
	limit?: number;
	verbose: boolean;
};

type ParsedCommandBar =
	| {noop: true}
	| {error: string}
	| {action: CommandAction};

function clampPage(page?: number) {
	if (page === undefined || !Number.isFinite(page) || page < 1) {
		return 1;
	}

	return Math.floor(page);
}

function clampLimit(limit?: number) {
	if (limit === undefined || !Number.isFinite(limit) || limit < 1) {
		return 10;
	}

	return Math.min(10, Math.floor(limit));
}

function buildInteractiveInvocation(
	options: InvocationOptions,
): InteractiveInvocation {
	return {
		interactive: true,
		defaultPage: clampPage(options.page),
		defaultLimit: clampLimit(options.limit),
		initialSearchMode: options.verbose ? 'verbose' : 'query',
	};
}

export function parseCliInvocation(
	input: string[],
	options: InvocationOptions,
): CliInvocation {
	if (input.length === 0) {
		return buildInteractiveInvocation(options);
	}

	const [command, ...rest] = input;
	const page = clampPage(options.page);
	const limit = clampLimit(options.limit);
	const query = rest.join(' ').trim();

	if (command === 'help') {
		return {
			interactive: false,
			action: {
				kind: 'help',
			},
		};
	}

	if (command === 'query') {
		return {
			interactive: false,
			action: {
				kind: 'search',
				mode: options.verbose ? 'verbose' : 'query',
				request: {
					query,
					page,
					limit,
				},
			},
		};
	}

	if (command === 'lucky') {
		return {
			interactive: false,
			action: {
				kind: 'lucky',
				request: {
					query,
					page,
					limit,
				},
			},
		};
	}

	if (command === 'crawler') {
		const crawlerCommand = rest[0] as
			| 'status'
			| 'start'
			| 'stop'
			| 'index'
			| undefined;
		return {
			interactive: false,
			action: {
				kind: 'crawler',
				command: crawlerCommand ?? 'status',
			},
		};
	}

	return {
		interactive: false,
		action: {
			kind: 'help',
		},
	};
}

function resolveSearchMode(command: string): SearchDisplayMode | undefined {
	if (command === 'query') {
		return 'query';
	}

	if (command === 'verbose') {
		return 'verbose';
	}

	return undefined;
}

export function parseCommandBar(
	commandBuffer: string,
	currentQuery: string,
	defaultPage: number,
	defaultLimit: number,
): ParsedCommandBar {
	const trimmed = commandBuffer.trim();
	if (trimmed.length === 0) {
		return {noop: true};
	}

	const spaceIndex = trimmed.indexOf(' ');
	const command =
		spaceIndex === -1 ? trimmed : trimmed.slice(0, Math.max(0, spaceIndex));
	const rest =
		spaceIndex === -1 ? '' : trimmed.slice(Math.max(0, spaceIndex + 1)).trim();

	const mode = resolveSearchMode(command);
	if (mode) {
		return {
			action: {
				kind: 'search',
				mode,
				request: {
					query: rest.length > 0 ? rest : currentQuery,
					page: defaultPage,
					limit: defaultLimit,
				},
			},
		};
	}

	if (command === 'lucky') {
		return {
			action: {
				kind: 'lucky',
				request: {
					query: rest.length > 0 ? rest : currentQuery,
					page: defaultPage,
					limit: defaultLimit,
				},
			},
		};
	}

	if (command === 'crawler') {
		if (
			rest === 'status' ||
			rest === 'start' ||
			rest === 'stop' ||
			rest === 'index'
		) {
			return {
				action: {
					kind: 'crawler',
					command: rest,
				},
			};
		}

		return {
			error: 'Crawler command must be one of: status, start, stop, index.',
		};
	}

	if (command === 'help') {
		return {
			action: {
				kind: 'help',
			},
		};
	}

	if (command === 'quit') {
		return {
			action: {
				kind: 'quit',
			},
		};
	}

	return {
		error: `Unknown command: ${command}`,
	};
}
