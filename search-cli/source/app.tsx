import React, {useMemo, useRef, useState} from 'react';
import {Box, Text, useApp, useInput} from 'ink';

import BrowseView from '../components/BrowseView.js';
import ResultsList from '../components/ResultsList.js';
import SearchBox from '../components/SearchBox.js';
import StatsDashboard from '../components/StatsDashboard.js';
import StatusBar from '../components/StatusBar.js';
import {runCliAction} from '../lib/actions.js';
import {parseCommandBar} from '../lib/commands.js';
import {
	getBannerLines,
	getCompactHeader,
	getHelpLines,
	getWelcomeLines,
} from '../lib/presentation.js';
import type {
	ActionRunner,
	CliConfig,
	CommandAction,
	SearchDisplayMode,
	UiMode,
} from '../lib/types.js';

type Props = {
	config: CliConfig;
	defaultPage: number;
	defaultLimit: number;
	initialSearchMode: SearchDisplayMode;
	runner?: ActionRunner;
};

function readErrorMessage(error: unknown) {
	return error instanceof Error ? error.message : 'Unknown CLI error.';
}

export default function App({
	config,
	defaultPage,
	defaultLimit,
	initialSearchMode,
	runner = runCliAction,
}: Readonly<Props>) {
	const {exit} = useApp();
	const [uiMode, setUiMode] = useState<UiMode>('normal');
	const [query, setQuery] = useState('');
	const [commandBuffer, setCommandBuffer] = useState('');
	const [searchMode, setSearchMode] =
		useState<SearchDisplayMode>(initialSearchMode);
	const [status, setStatus] = useState(
		'Ready. Press i to search, : for commands, Esc to leave a mode, q to quit, or Ctrl+C anytime.',
	);
	const [lines, setLines] = useState<string[]>(getWelcomeLines());
	const [busy, setBusy] = useState(false);
	const [hasInteracted, setHasInteracted] = useState(false);
	const [showHelp, setShowHelp] = useState(false);
	const [showStats, setShowStats] = useState(false);
	const [showBrowser, setShowBrowser] = useState(false);
	const [page, setPage] = useState(defaultPage);
	const bannerLines = useMemo(() => getBannerLines(), []);

	// Track the last search action for pagination replay
	const lastSearchActionRef = useRef<CommandAction | null>(null);

	async function executeAction(action: CommandAction) {
		if (action.kind === 'quit') {
			exit();
			return;
		}

		if (action.kind === 'help') {
			setShowHelp(true);
			setHasInteracted(true);
			setStatus('Help opened.');
			setUiMode('normal');
			setCommandBuffer('');
			return;
		}

		if (action.kind === 'stats') {
			setShowStats(true);
			setHasInteracted(true);
			setStatus('Stats dashboard opened.');
			setUiMode('normal');
			setCommandBuffer('');
			return;
		}

		if (action.kind === 'open') {
			setShowBrowser(true);
			setHasInteracted(true);
			setStatus('Browser view opened.');
			setUiMode('normal');
			setCommandBuffer('');
			return;
		}

		if (action.kind === 'paginate') {
			const lastAction = lastSearchActionRef.current;
			if (
				!lastAction ||
				(lastAction.kind !== 'search' && lastAction.kind !== 'lucky')
			) {
				setStatus('No previous search to paginate.');
				setUiMode('normal');
				setCommandBuffer('');
				return;
			}

			let nextPage: number;
			if (action.direction === 'next') {
				nextPage = page + 1;
			} else if (action.direction === 'prev') {
				nextPage = Math.max(1, page - 1);
			} else {
				nextPage = Math.max(1, action.page ?? 1);
			}

			setPage(nextPage);

			// Replay the last search with the new page
			const replayAction: CommandAction =
				lastAction.kind === 'search'
					? {
							...lastAction,
							request: {...lastAction.request, page: nextPage},
					  }
					: {
							...lastAction,
							request: {...lastAction.request, page: nextPage},
					  };

			setUiMode('normal');
			setCommandBuffer('');
			void executeAction(replayAction);
			return;
		}

		setBusy(true);
		setHasInteracted(true);
		setShowHelp(false);
		setStatus('Running command...');

		// Store the action for pagination replay
		if (action.kind === 'search' || action.kind === 'lucky') {
			lastSearchActionRef.current = action;
		}

		try {
			const result = await runner(action, {config});
			if (result.shouldExit) {
				exit();
				return;
			}

			// Stats sentinel from action runner
			if (result.view === 'stats') {
				setShowStats(true);
				setStatus('Stats dashboard opened.');
				setBusy(false);
				setUiMode('normal');
				setCommandBuffer('');
				return;
			}

			// Open sentinel from action runner
			if (result.view === 'open') {
				setShowBrowser(true);
				setStatus('Browser view opened.');
				setBusy(false);
				setUiMode('normal');
				setCommandBuffer('');
				return;
			}

			if (result.query !== undefined) {
				setQuery(result.query);
			}

			if (result.searchMode) {
				setSearchMode(result.searchMode);
			}

			setStatus(result.status);
			setLines(result.lines.length > 0 ? result.lines : getWelcomeLines());
			setShowHelp(result.view === 'help');
		} catch (error: unknown) {
			setStatus('Command failed.');
			setLines([readErrorMessage(error)]);
		} finally {
			setBusy(false);
			setUiMode('normal');
			setCommandBuffer('');
		}
	}

	useInput((input, key) => {
		if (key?.ctrl && input === 'c') {
			exit();
			return;
		}

		if (busy || showStats) {
			return;
		}

		if (uiMode === 'insert') {
			if (key?.escape) {
				setUiMode('normal');
				setStatus('Insert mode closed.');
				return;
			}

			if (key?.return) {
				void executeAction({
					kind: 'search',
					mode: searchMode,
					request: {
						query,
						page,
						limit: defaultLimit,
					},
				});
				return;
			}

			if (key?.backspace || key?.delete) {
				setQuery(previousQuery => previousQuery.slice(0, -1));
				return;
			}

			if (!key?.ctrl && input.length > 0) {
				setQuery(previousQuery => previousQuery + input);
			}

			return;
		}

		if (uiMode === 'command') {
			if (input === 'q' && commandBuffer.length === 0) {
				exit();
				return;
			}

			if (key?.escape) {
				setUiMode('normal');
				setCommandBuffer('');
				setStatus('Command mode closed.');
				return;
			}

			if (key?.return) {
				const parsedCommand = parseCommandBar(
					commandBuffer,
					query,
					page,
					defaultLimit,
				);
				if ('noop' in parsedCommand) {
					setUiMode('normal');
					setCommandBuffer('');
					return;
				}

				if ('error' in parsedCommand) {
					setStatus(parsedCommand.error);
					setLines(getHelpLines());
					setShowHelp(true);
					setUiMode('normal');
					setCommandBuffer('');
					setHasInteracted(true);
					return;
				}

				void executeAction(parsedCommand.action);
				return;
			}

			if (key?.backspace || key?.delete) {
				setCommandBuffer(previousValue => previousValue.slice(0, -1));
				return;
			}

			if (!key?.ctrl && input.length > 0) {
				setCommandBuffer(previousValue => previousValue + input);
			}

			return;
		}

		if (input === 'q') {
			exit();
			return;
		}

		if (input === 'i') {
			setUiMode('insert');
			setStatus('Insert mode ready.');
			setHasInteracted(true);
			return;
		}

		if (input === ':') {
			setUiMode('command');
			setCommandBuffer('');
			setStatus('Command mode ready.');
			setHasInteracted(true);
			return;
		}

		if (input === '?') {
			setShowHelp(previousValue => !previousValue);
			setStatus('Help toggled.');
			setHasInteracted(true);
		}
	});

	// Full-screen stats overlay
	if (showStats) {
		return (
			<StatsDashboard
				config={config}
				once={false}
				onExit={() => {
					setShowStats(false);
					setStatus('Returned from stats dashboard.');
				}}
			/>
		);
	}

	// Full-screen browser/tunnel overlay
	if (showBrowser) {
		return (
			<BrowseView
				config={config}
				localPort={8080}
				onExit={() => {
					setShowBrowser(false);
					setStatus('Returned from browser view.');
				}}
			/>
		);
	}

	const visibleLines = showHelp ? getHelpLines() : lines;

	return (
		<Box flexDirection="column">
			{hasInteracted ? (
				<Text bold color="cyanBright">
					{getCompactHeader(searchMode)}
				</Text>
			) : (
				<Box flexDirection="column">
					{bannerLines.map((line: string) => (
						<Text key={line} bold color="cyanBright">
							{line}
						</Text>
					))}
				</Box>
			)}

			<StatusBar
				uiMode={uiMode}
				searchMode={searchMode}
				status={status}
				serverUrl={config.serverUrl}
				isBusy={busy}
				sshHost={config.ssh?.host}
			/>
			<SearchBox uiMode={uiMode} query={query} commandBuffer={commandBuffer} />
			<ResultsList lines={visibleLines} />
		</Box>
	);
}
