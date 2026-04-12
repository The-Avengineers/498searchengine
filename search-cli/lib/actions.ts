import {spawn} from 'node:child_process';

import {requestSearch} from './api.js';
import {runCrawlerCommand} from './crawler.js';
import {
	formatLuckyLines,
	formatSearchLines,
	getHelpLines,
} from './presentation.js';
import type {
	ActionDependencies,
	ActionResult,
	ActionRunner,
	CliConfig,
	LuckyOpenResult,
} from './types.js';

async function openLuckyUrl(
	url: string,
	config: CliConfig,
): Promise<LuckyOpenResult> {
	const openCommand = config.platform === 'darwin' ? 'open' : 'xdg-open';

	return new Promise(resolve => {
		const child = spawn(openCommand, [url], {
			detached: true,
			stdio: 'ignore',
		});

		child.on('error', error => {
			resolve({
				opened: false,
				detail: `Could not open browser automatically: ${error.message}`,
			});
		});

		child.unref();
		resolve({
			opened: true,
			detail: `Opened ${url} with ${openCommand}.`,
		});
	});
}

function readBlankQueryResult(): ActionResult {
	return {
		status: 'Blank query refused.',
		lines: ['Enter a query to search.'],
		exitCode: 1,
		view: 'results',
	};
}

export const runCliAction: ActionRunner = async (action, options) => {
	const dependencies: Required<ActionDependencies> = {
		searchRequest: options.dependencies?.searchRequest ?? requestSearch,
		runCrawlerCommand:
			options.dependencies?.runCrawlerCommand ?? runCrawlerCommand,
		openLuckyUrl: options.dependencies?.openLuckyUrl ?? openLuckyUrl,
	};

	if (action.kind === 'help') {
		return {
			status: 'Help loaded.',
			lines: getHelpLines(),
			exitCode: 0,
			view: 'help',
		};
	}

	if (action.kind === 'quit') {
		return {
			status: 'Quit requested.',
			lines: [],
			exitCode: 0,
			view: 'results',
			shouldExit: true,
		};
	}

	if (action.kind === 'crawler') {
		const crawlerResult = await dependencies.runCrawlerCommand(
			options.config,
			action.command,
		);
		return {
			status: crawlerResult.status,
			lines: crawlerResult.lines,
			exitCode: crawlerResult.exitCode,
			view: 'results',
		};
	}

	if (action.request.query.trim().length === 0) {
		return readBlankQueryResult();
	}

	if (action.kind === 'search') {
		const response = await dependencies.searchRequest(
			options.config,
			action.request,
		);
		return {
			status:
				action.mode === 'verbose' ? 'Verbose query loaded.' : 'Query loaded.',
			lines: formatSearchLines(
				response,
				action.mode,
				options.config.linkSupport,
			),
			exitCode: 0,
			view: 'results',
			query: action.request.query,
			searchMode: action.mode,
		};
	}

	const response = await dependencies.searchRequest(
		options.config,
		action.request,
	);
	const openResult =
		response.results.length > 0
			? await dependencies.openLuckyUrl(
					response.results[0]!.url,
					options.config,
			  )
			: {
					opened: false,
					detail: 'No browser opened.',
			  };

	return {
		status: 'Lucky result loaded.',
		lines: formatLuckyLines(response, options.config.linkSupport, openResult),
		exitCode: 0,
		view: 'results',
		query: action.request.query,
	};
};
