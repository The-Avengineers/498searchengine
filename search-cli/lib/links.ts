export function formatTerminalLink(
	label: string,
	url: string,
	linkSupport: boolean,
) {
	if (!linkSupport) {
		return label;
	}

	return `\u001B]8;;${url}\u0007${label}\u001B]8;;\u0007`;
}
