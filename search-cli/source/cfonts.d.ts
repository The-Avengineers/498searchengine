declare module 'cfonts' {
	type RenderOptions = {
		font?: string;
		align?: string;
		colors?: string[];
		backgroundColor?: string;
		letterSpacing?: number;
		lineHeight?: number;
		space?: boolean;
		maxLength?: number;
	};

	type RenderResult = {
		string: string;
	};

	const CFonts: {
		render: (text: string, options?: RenderOptions) => RenderResult | false;
	};

	export default CFonts;
}
