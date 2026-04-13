import React from 'react';
import {Text} from 'ink';

import type {UiMode} from '../lib/types.js';

type Props = {
	uiMode: UiMode;
	query: string;
	commandBuffer: string;
};

function readQueryLabel(query: string) {
	return query.length > 0 ? query : '<empty>';
}

export default function SearchBox({
	uiMode,
	query,
	commandBuffer,
}: Readonly<Props>) {
	if (uiMode === 'insert') {
		return <Text>INSERT :: search&gt; {query}|</Text>;
	}

	if (uiMode === 'command') {
		return <Text>COMMAND :: :{commandBuffer}|</Text>;
	}

	return <Text>NORMAL :: query&gt; {readQueryLabel(query)}</Text>;
}
