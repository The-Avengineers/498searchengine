import React, {useEffect, useRef, useState} from 'react';
import {Box, Text, useApp, useInput} from 'ink';

import {launchBrowser} from '../lib/open.js';
import type {CliConfig} from '../lib/types.js';
import type {OpenPhase, OpenSession} from '../lib/open.js';

type Props = {
	config: CliConfig;
	localPort: number;
	onExit?: () => void;
};

function phaseLabel(phase: OpenPhase): string {
	switch (phase.kind) {
		case 'active': return `Browser opened at ${phase.url}`;
		case 'error': return phase.message;
	}
}

function phaseColor(phase: OpenPhase): string {
	if (phase.kind === 'error') return 'red';
	if (phase.kind === 'active') return 'green';
	return 'yellow';
}

function Spinner({active}: {readonly active: boolean}) {
	const frames = ['⠋', '⠙', '⠹', '⠸', '⠼', '⠴', '⠦', '⠧', '⠇', '⠏'];
	const [frame, setFrame] = useState(0);

	useEffect(() => {
		if (!active) return;
		const interval = setInterval(() => {
			setFrame(f => (f + 1) % frames.length);
		}, 80);
		return () => {
			clearInterval(interval);
		};
	}, [active, frames.length]);

	if (!active) return null;
	return <Text color="cyan">{frames[frame]} </Text>;
}

function Uptime({startedAt}: {readonly startedAt: Date}) {
	const [elapsed, setElapsed] = useState(0);

	useEffect(() => {
		const interval = setInterval(() => {
			setElapsed(Math.floor((Date.now() - startedAt.getTime()) / 1000));
		}, 1000);
		return () => {
			clearInterval(interval);
		};
	}, [startedAt]);

	const h = Math.floor(elapsed / 3600);
	const m = Math.floor((elapsed % 3600) / 60);
	const s = elapsed % 60;
	const formatted =
		h > 0
			? `${h}h ${m}m ${s}s`
			: m > 0
			? `${m}m ${s}s`
			: `${s}s`;

	return <Text dimColor>uptime: {formatted}</Text>;
}

export default function BrowseView({config, localPort: _localPort, onExit}: Readonly<Props>) {
	const {exit} = useApp();
	const [phase, setPhase] = useState<OpenPhase>({
		kind: 'active',
		url: config.serverUrl,
		startedAt: new Date(),
	});
	const sessionRef = useRef<OpenSession | null>(null);

	const handleExit = React.useCallback(() => {
		sessionRef.current?.cleanup();
		if (onExit) {
			onExit();
		} else {
			exit();
		}
	}, [onExit, exit]);

	useInput((input, key) => {
		if (input === 'q' || (key?.ctrl && input === 'c')) {
			handleExit();
		}
	});

	useEffect(() => {
		let mounted = true;

		launchBrowser(config, _localPort, nextPhase => {
			if (mounted) setPhase(nextPhase);
		}).then(session => {
			if (mounted) {
				sessionRef.current = session;
			} else {
				// Component unmounted before launch finished — clean up immediately
				session.cleanup();
			}
		}).catch(() => {
			// Errors are surfaced via onPhase callback
		});

		return () => {
			mounted = false;
			sessionRef.current?.cleanup();
		};
	}, [config, _localPort, handleExit]);

	return (
		<Box flexDirection="column">
			<Text bold color="cyanBright">
				AVENGINE BROWSER
			</Text>

			<Box marginTop={1}>
				<Spinner active={false} />
				<Text color={phaseColor(phase)}>{phaseLabel(phase)}</Text>
			</Box>

			{phase.kind === 'active' && (
				<Box flexDirection="column" marginTop={1}>
					<Box>
						<Text>{'url:    '}</Text>
						<Text bold color="cyan">{phase.url}</Text>
					</Box>
					<Box marginTop={1}>
						<Uptime startedAt={phase.startedAt} />
					</Box>
				</Box>
			)}

			{phase.kind === 'error' && (
				<Box marginTop={1}>
					<Text dimColor>Press q or Ctrl+C to exit.</Text>
				</Box>
			)}

			{phase.kind === 'active' && (
				<Text dimColor>
					Browser launched against the configured AVENGINE server. Press q or Ctrl+C to exit.
				</Text>
			)}
		</Box>
	);
}
