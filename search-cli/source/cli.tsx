#!/usr/bin/env node
import process from 'node:process';

import React from 'react';
import meow from 'meow';
import {render} from 'ink';

import BrowseView from '../components/BrowseView.js';
import StatsDashboard from '../components/StatsDashboard.js';
import SystemStatus from '../components/SystemStatus.js';
import {runCliAction} from '../lib/actions.js';
import {parseCliInvocation} from '../lib/commands.js';
import {resolveCliConfig} from '../lib/config.js';
import {
	readPersistentConfigSafe,
	runConfigCommand,
} from '../lib/persistent-config.js';
import {getHelpLines} from '../lib/presentation.js';
import App from './app.js';

const helpText = `
Usage
  $ avengine
  $ avengine query "<query>" [--server URL] [--page N] [--limit N] [--verbose]
  $ avengine lucky "<query>" [--server URL]
  $ avengine crawler status|start|stop|index|watch|profile
  $ avengine stats [--once]
  $ avengine status
  $ avengine profile
  $ avengine open [--port 8080]
  $ avengine config show
  $ avengine config set --server URL --ssh-host HOST --ssh-port 22222 --ssh-key PATH
  $ avengine config clear

Options
  --server   Search server URL (overrides AVENGINE_SERVER_URL)
  --host     SSH host for remote container (overrides AVENGINE_SSH_HOST)
  --ssh-host SSH host to save with "config set"
  --ssh-port SSH port to save with "config set"
  --ssh-user SSH user to save with "config set"
  --ssh-key  SSH private key path to save with "config set"
  --page     1-based page number
  --limit    Result page size
  --verbose  Run query mode in verbose mode
  --once     For stats: fetch once then exit (no live refresh)
  --port     Local port for the browser tunnel (default 8080, used with open)

Remote access
  Set AVENGINE_SSH_HOST to connect to a deploy container via SSH.
  AVENGINE_SSH_PORT (default 2222), AVENGINE_SSH_USER (default engine),
  and AVENGINE_SSH_KEY (path to identity file) are also supported.

Examples
  $ avengine
  $ avengine query "apple" --verbose
  $ avengine lucky "apple"
  $ avengine crawler status
  $ avengine crawler watch
  $ avengine stats
  $ avengine stats --once
  $ avengine status
  $ avengine profile
  $ avengine open
  $ avengine open --port 9090
  $ avengine config set --server https://theavengine.com --ssh-host theavengine.com --ssh-port 22222 --ssh-key ~/.ssh/id_ed25519
  $ AVENGINE_SSH_HOST=myserver avengine open
  $ AVENGINE_SSH_HOST=myserver avengine stats
  $ AVENGINE_SSH_HOST=myserver avengine query "apple"
`;

const cli = meow(helpText, {
	importMeta: import.meta,
	flags: {
		server: {
			type: 'string',
		},
		host: {
			type: 'string',
		},
		sshHost: {
			type: 'string',
		},
		sshPort: {
			type: 'number',
		},
		sshUser: {
			type: 'string',
		},
		sshKey: {
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
		once: {
			type: 'boolean',
			default: false,
		},
		port: {
			type: 'number',
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
	const flags = cli.flags as typeof cli.flags & {
		sshHost?: string;
		sshPort?: number;
		sshUser?: string;
		sshKey?: string;
	};

	if (cli.input[0] === 'config') {
		const result = runConfigCommand(cli.input.slice(1), {
			server: flags.server,
			host: flags.host,
			sshHost: flags.sshHost,
			sshPort: flags.sshPort,
			sshUser: flags.sshUser,
			sshKey: flags.sshKey,
		});
		console.log(result.lines.join('\n'));
		process.exitCode = result.exitCode;
		return;
	}

	const config = resolveCliConfig();

	if (flags.server) {
		config.serverUrl = flags.server;
	}

	// --host overrides AVENGINE_SSH_HOST
	if (flags.host) {
		if (config.ssh) {
			config.ssh.host = flags.host;
		} else {
			config.ssh = {
				host: flags.host,
				port: 2222,
				user: 'engine',
			};
		}

		const persistentConfig = readPersistentConfigSafe();
		if (
			!process.env['AVENGINE_SERVER_URL'] &&
			!flags.server &&
			!persistentConfig.serverUrl
		) {
			config.serverUrl = `http://${flags.host}:8080`;
		}
	}

	const invocation = parseCliInvocation(cli.input, {
		server: flags.server,
		host: flags.host,
		page: flags.page,
		limit: flags.limit,
		verbose: flags.verbose,
		once: flags.once,
		port: flags.port,
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

	// Stats dashboard — renders as a live Ink app
	if (invocation.action.kind === 'stats') {
		clearInteractiveScreen();
		render(<StatsDashboard config={config} once={invocation.action.once} />);
		return;
	}

	// System status — one-shot Ink render then exits
	if (invocation.action.kind === 'status') {
		render(<SystemStatus config={config} />);
		return;
	}

	// Open browser — start server + SSH tunnel + open browser tab
	if (invocation.action.kind === 'open') {
		clearInteractiveScreen();
		render(
			<BrowseView config={config} localPort={invocation.action.localPort} />,
		);
		return;
	}

	const result = await runCliAction(invocation.action, {config});
	if (result.lines.length > 0) {
		console.log(result.lines.join('\n'));
	}

	process.exitCode = result.exitCode;
}

await main();
