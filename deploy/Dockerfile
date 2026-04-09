FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
  build-essential \
  cmake \
  libssl-dev \
  zlib1g-dev \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt /src/CMakeLists.txt
COPY include /src/include
COPY src /src/src
COPY tests /src/tests
COPY deploy /src/deploy
COPY profiling /src/profiling
COPY flake.nix /src/flake.nix
COPY flake.lock /src/flake.lock
COPY seed_list.txt /src/seed_list.txt
COPY banned_urls.txt /src/banned_urls.txt
COPY banned_words.txt /src/banned_words.txt
RUN cmake -S /src -B /src/build -DCMAKE_BUILD_TYPE=Release \
  && cmake --build /src/build --target \
       crawler_main \
       crawler_index_profile \
       search_server \
       index_builder \
       anchor_processor \
       -j"$(nproc)"

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
  openssh-server \
  ca-certificates \
  python3 \
  libssl3t64 \
  zlib1g \
  && rm -rf /var/lib/apt/lists/* \
  && mkdir -p /var/run/sshd

RUN useradd -m -s /bin/bash engine
RUN mkdir -p /home/engine/.ssh && chown -R engine:engine /home/engine/.ssh && chmod 700 /home/engine/.ssh

RUN mkdir -p /avengine/bin /avengine/config /avengine/profiling/scripts \
    /avengine/profiling/fixtures /data /run/avengine \
    && ln -s /avengine /opt/avengine \
    && ln -s /avengine/config /config \
    && ln -s /avengine/profiling /profiling \
    && ln -s /data /var/lib/avengine \
    && ln -s /data/logs /logs \
    && ln -s /data/crawlers /crawlers \
    && ln -s /data/corpus-index/current /corpus

COPY --from=builder /src/build/crawler_main /avengine/bin/crawler_main
COPY --from=builder /src/build/crawler_index_profile /avengine/bin/crawler_index_profile
COPY --from=builder /src/build/search_server /avengine/bin/search_server
COPY --from=builder /src/build/index_builder /avengine/bin/index_builder
COPY --from=builder /src/build/anchor_processor /avengine/bin/anchor_processor
COPY --from=builder /src/deploy/start-crawler-swarm.sh /avengine/bin/start-crawler-swarm.sh
COPY --from=builder /src/deploy/stop-crawler-swarm.sh /avengine/bin/stop-crawler-swarm.sh
COPY --from=builder /src/deploy/start-crawler-supervisor.sh /avengine/bin/start-crawler-supervisor.sh
COPY --from=builder /src/deploy/stop-crawler-supervisor.sh /avengine/bin/stop-crawler-supervisor.sh
COPY --from=builder /src/deploy/aggregate-corpus-index.sh /avengine/bin/aggregate-corpus-index.sh
COPY --from=builder /src/deploy/promote-corpus-profile.sh /avengine/bin/promote-corpus-profile.sh
COPY --from=builder /src/deploy/start-aggregate-watch.sh /avengine/bin/start-aggregate-watch.sh
COPY --from=builder /src/deploy/stop-aggregate-watch.sh /avengine/bin/stop-aggregate-watch.sh
COPY --from=builder /src/deploy/start-search-server.sh /avengine/bin/start-search-server.sh
COPY --from=builder /src/deploy/stop-search-server.sh /avengine/bin/stop-search-server.sh
COPY --from=builder /src/deploy/avengine-status.sh /avengine/bin/avengine-status.sh
COPY --from=builder /src/deploy/avengine-start.sh /avengine/bin/avengine-start.sh
COPY --from=builder /src/deploy/avengine-stop.sh /avengine/bin/avengine-stop.sh
COPY --from=builder /src/deploy/avengine-help.sh /avengine/bin/avengine-help.sh
COPY --from=builder /src/deploy/avengine-stats.sh /avengine/bin/avengine-stats.sh
COPY --from=builder /src/deploy/avengine-watch.sh /avengine/bin/avengine-watch.sh
COPY --from=builder /src/deploy/avengine-profile.sh /avengine/bin/avengine-profile.sh
COPY --from=builder /src/seed_list.txt /avengine/config/seed_list.txt
COPY --from=builder /src/banned_urls.txt /avengine/config/banned_urls.txt
COPY --from=builder /src/banned_words.txt /avengine/config/banned_words.txt
COPY --from=builder /src/flake.nix /avengine/flake.nix
COPY --from=builder /src/flake.lock /avengine/flake.lock
COPY --from=builder /src/profiling/run_profiles.sh /avengine/profiling/run_profiles.sh
COPY --from=builder /src/profiling/scripts/render_ci_profile_summary.py /avengine/profiling/scripts/render_ci_profile_summary.py
COPY --from=builder /src/profiling/scripts/profile_query_pipeline.sh /avengine/profiling/scripts/profile_query_pipeline.sh
COPY --from=builder /src/profiling/scripts/run_search_http_profile.py /avengine/profiling/scripts/run_search_http_profile.py
COPY --from=builder /src/profiling/scripts/query_fixture.py /avengine/profiling/scripts/query_fixture.py
COPY --from=builder /src/profiling/fixtures/search_corpus.tsv /avengine/profiling/fixtures/search_corpus.tsv
COPY --from=builder /src/profiling/fixtures/search_queries.tsv /avengine/profiling/fixtures/search_queries.tsv
COPY --from=builder /src/profiling/fixtures/live_search_queries.tsv /avengine/profiling/fixtures/live_search_queries.tsv
COPY --from=builder /src/profiling/fixtures/crawl_pages /avengine/profiling/fixtures/crawl_pages
COPY deploy/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh \
    /avengine/bin/start-crawler-swarm.sh \
    /avengine/bin/stop-crawler-swarm.sh \
    /avengine/bin/start-crawler-supervisor.sh \
    /avengine/bin/stop-crawler-supervisor.sh \
    /avengine/bin/aggregate-corpus-index.sh \
    /avengine/bin/promote-corpus-profile.sh \
    /avengine/bin/start-aggregate-watch.sh \
    /avengine/bin/stop-aggregate-watch.sh \
    /avengine/bin/start-search-server.sh \
    /avengine/bin/stop-search-server.sh \
    /avengine/bin/avengine-status.sh \
    /avengine/bin/avengine-start.sh \
    /avengine/bin/avengine-stop.sh \
    /avengine/bin/avengine-help.sh \
    /avengine/bin/avengine-stats.sh \
    /avengine/bin/avengine-watch.sh \
    /avengine/bin/avengine-profile.sh \
    /avengine/profiling/run_profiles.sh \
    /avengine/profiling/scripts/profile_query_pipeline.sh \
    && ln -s /avengine/bin/avengine-start.sh /usr/local/bin/av-start \
    && ln -s /avengine/bin/avengine-stop.sh /usr/local/bin/av-stop \
    && ln -s /avengine/bin/avengine-help.sh /usr/local/bin/av-help \
    && ln -s /avengine/bin/avengine-stats.sh /usr/local/bin/av-stats \
    && ln -s /avengine/bin/avengine-watch.sh /usr/local/bin/av-watch \
    && ln -s /avengine/bin/avengine-profile.sh /usr/local/bin/av-profile \
    && ln -s /avengine/bin/start-crawler-supervisor.sh /usr/local/bin/av-start-crawlers \
    && ln -s /avengine/bin/stop-crawler-supervisor.sh /usr/local/bin/av-stop-crawler-supervisor \
    && ln -s /avengine/bin/stop-crawler-swarm.sh /usr/local/bin/av-stop-crawlers \
    && ln -s /avengine/bin/start-aggregate-watch.sh /usr/local/bin/av-start-aggregate \
    && ln -s /avengine/bin/stop-aggregate-watch.sh /usr/local/bin/av-stop-aggregate \
    && ln -s /avengine/bin/promote-corpus-profile.sh /usr/local/bin/av-promote-corpus-profile \
    && ln -s /avengine/bin/start-search-server.sh /usr/local/bin/av-serve \
    && ln -s /avengine/bin/stop-search-server.sh /usr/local/bin/av-stop-search \
    && ln -s /avengine/bin/avengine-status.sh /usr/local/bin/av-status \
    && printf 'export PATH=/avengine/bin:$PATH\n' > /etc/profile.d/avengine-path.sh \
    && cat > /etc/profile.d/avengine-shell.sh <<'EOF'
if [[ $- == *i* ]]; then
  cd /
  echo
  echo "AVEngine container"
  echo "  Layout: /avengine  /config  /data  /logs  /crawlers  /corpus  /profiling"
  echo "  Commands: av-help  av-status  av-stats  av-watch [seconds]  av-profile"
  echo "            av-start  av-stop  av-serve [port] [index-dir] [--daemon]"
  echo "            av-start-crawlers  av-stop-crawler-supervisor  av-stop-crawlers"
  echo "            av-start-aggregate  av-stop-aggregate  av-promote-corpus-profile  av-stop-search"
  echo "  Nix: /avengine/flake.nix"
  echo
fi
EOF
RUN chown -R engine:engine /avengine /data /home/engine/.ssh /run/avengine

EXPOSE 22
CMD ["/usr/local/bin/entrypoint.sh"]
