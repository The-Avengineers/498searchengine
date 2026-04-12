#!/usr/bin/env node
import process from 'node:process';

import React from 'react';
import meow from 'meow';
import {render} from 'ink';

import {runCliAction} from '../lib/actions.js';
import {parseCliInvocation} from '../lib/commands.js';
import {resolveCliConfig} from '../lib/config.js';
import {getHelpLines} from '../lib/presentation.js';
import App from './app.js';

const helpText = `
Usage
  $ avengine
  $ avengine query "<query>" [--server URL] [--page N] [--limit N] [--verbose]
  $ avengine lucky "<query>" [--server URL]
  $ avengine crawler status|start|stop|index

Options
  --server   Search server URL
  --page     1-based page number
  --limit    Result page size
  --verbose  Run query mode in verbose mode

Examples
  $ avengine
  $ avengine query "apple"
  $ avengine query "apple" --verbose
  $ avengine lucky "apple"
  $ avengine crawler status
`;

const cli = meow(helpText, {
	importMeta: import.meta,
	flags: {
		server: {
			type: 'string',
		},
		page: {
			type: 'number',
		},
		limit: {
			type: 'number',
		},
		verbose: {
			type: 'boolean',
			default: false,
		},
	},
});

function clearInteractiveScreen() {
	if (!process.stdout.isTTY) {
		return;
	}

	process.stdout.write('\u001B[2J\u001B[3J\u001B[H');
}

async function main() {
	const config = resolveCliConfig();
	if (cli.flags.server) {
		config.serverUrl = cli.flags.server;
	}

	const invocation = parseCliInvocation(cli.input, {
		server: cli.flags.server,
		page: cli.flags.page,
		limit: cli.flags.limit,
		verbose: cli.flags.verbose,
	});

	if (invocation.interactive) {
		clearInteractiveScreen();
		render(
			<App
				config={config}
				defaultPage={invocation.defaultPage}
				defaultLimit={invocation.defaultLimit}
				initialSearchMode={invocation.initialSearchMode}
			/>,
		);
		return;
	}

	if (invocation.action.kind === 'help') {
		console.log(getHelpLines().join('\n'));
		return;
	}

	const result = await runCliAction(invocation.action, {config});
	if (result.lines.length > 0) {
		console.log(result.lines.join('\n'));
	}

	process.exitCode = result.exitCode;
}

await main();
