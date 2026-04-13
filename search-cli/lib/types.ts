export type SearchDisplayMode = 'query' | 'verbose';
export type UiMode = 'normal' | 'insert' | 'command';
export type CrawlerCommand = 'status' | 'start' | 'stop' | 'index' | 'watch' | 'profile';
export type ActionView = 'results' | 'help' | 'stats' | 'open';

export type SshConfig = {
	host: string;
	port: number;
	user: string;
	keyPath?: string;
};

export type CliConfig = {
	serverUrl: string;
	runtimeBinRoot: string;
	dataRoot: string;
	configRoot: string;
	platform: NodeJS.Platform;
	linkSupport: boolean;
	italicSupport: boolean;
	ssh?: SshConfig;
};

export type SearchRequest = {
	query: string;
	page: number;
	limit: number;
};

export type SearchError = {
	offset: number;
	message: string;
};

export type SearchResult = {
	url: string;
	title: string;
	score: number;
};

export type SearchResponse = {
	query: string;
	page: number;
	limit: number;
	totalHits: number;
	tookMs: number;
	parseMs: number;
	solveMs: number;
	collectMs: number;
	rankMs: number;
	errors: SearchError[];
	results: SearchResult[];
};

export type LuckyOpenResult = {
	opened: boolean;
	detail: string;
};

export type CrawlerCommandResult = {
	status: string;
	lines: string[];
	exitCode: number;
};

export type WorkerStats = {
	workerId: string;
	state: 'running' | 'stopped' | 'stale-pid';
	sfiFiles: number;
	crawlRows: number;
	acLogMb: number;
	checkpoint: boolean;
	docsPerSec?: number;
};

export type StatsSnapshot = {
	timestamp: number;
	rowsByWorker: Record<string, number>;
};

export type CrawlerStats = {
	workers: WorkerStats[];
	totalWorkerShards: number;
	currentCorpusShards: number;
};

export type SystemStatusData = {
	searchServerOk: boolean;
	searchServerLatencyMs?: number;
	crawlerSwarm: string;
	dataUsage: string;
	corpusShards: number;
};

export type CommandAction =
	| {
			kind: 'search';
			mode: SearchDisplayMode;
			request: SearchRequest;
	  }
	| {
			kind: 'lucky';
			request: SearchRequest;
	  }
	| {
			kind: 'crawler';
			command: CrawlerCommand;
	  }
	| {
			kind: 'stats';
			once: boolean;
	  }
	| {
			kind: 'status';
	  }
	| {
			kind: 'profile';
	  }
	| {
			kind: 'open';
			localPort: number;
	  }
	| {
			kind: 'paginate';
			direction: 'next' | 'prev' | 'absolute';
			page?: number;
	  }
	| {
			kind: 'help';
	  }
	| {
			kind: 'quit';
	  };

export type ActionResult = {
	status: string;
	lines: string[];
	exitCode: number;
	view: ActionView;
	query?: string;
	searchMode?: SearchDisplayMode;
	shouldExit?: boolean;
};

export type ActionDependencies = {
	searchRequest?: (
		config: CliConfig,
		request: SearchRequest,
	) => Promise<SearchResponse>;
	runCrawlerCommand?: (
		config: CliConfig,
		command: CrawlerCommand,
	) => Promise<CrawlerCommandResult>;
	openLuckyUrl?: (url: string, config: CliConfig) => Promise<LuckyOpenResult>;
};

export type ActionRunner = (
	action: CommandAction,
	options: {
		config: CliConfig;
		dependencies?: ActionDependencies;
	},
) => Promise<ActionResult>;

export type ParseInvocation = {
	interactive: false;
	action: CommandAction;
};

export type InteractiveInvocation = {
	interactive: true;
	defaultPage: number;
	defaultLimit: number;
	initialSearchMode: SearchDisplayMode;
};

export type CliInvocation = ParseInvocation | InteractiveInvocation;
