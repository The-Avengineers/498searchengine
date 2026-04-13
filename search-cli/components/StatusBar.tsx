import React from 'react';
import {Text} from 'ink';

import type {SearchDisplayMode, UiMode} from '../lib/types.js';

type Props = {
	uiMode: UiMode;
	searchMode: SearchDisplayMode;
	status: string;
	serverUrl: string;
	isBusy: boolean;
	sshHost?: string;
};

function formatMode(mode: UiMode) {
	return mode.toUpperCase();
}

function formatSearchMode(mode: SearchDisplayMode) {
	return mode.toUpperCase();
}

export default function StatusBar({
	uiMode,
	searchMode,
	status,
	serverUrl,
	isBusy,
	sshHost,
}: Readonly<Props>) {
	return (
		<Text>
			{formatMode(uiMode)} | {formatSearchMode(searchMode)} | {serverUrl}
			{sshHost ? (
				<Text color="yellow">{` [remote: ${sshHost}]`}</Text>
			) : (
				''
			)}{' '}
			| {isBusy ? 'busy' : status}
		</Text>
	);
}
