import React from 'react';
import {Box, Text} from 'ink';

type Props = {
	lines: string[];
};

export default function ResultsList({lines}: Readonly<Props>) {
	return (
		<Box flexDirection="column" marginTop={1}>
			{lines.map(line => (
				<Text key={line}>{line}</Text>
			))}
		</Box>
	);
}
