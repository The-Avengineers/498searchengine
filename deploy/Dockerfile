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
COPY seed_list.txt /src/seed_list.txt
COPY banned_urls.txt /src/banned_urls.txt
COPY banned_words.txt /src/banned_words.txt
RUN cmake -S /src -B /src/build -DCMAKE_BUILD_TYPE=Release \
  && cmake --build /src/build --target \
       crawler_main \
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

RUN mkdir -p /opt/avengine/bin /opt/avengine/config /opt/avengine/profiling/scripts \
    /opt/avengine/profiling/fixtures /var/lib/avengine

COPY --from=builder /src/build/crawler_main /opt/avengine/bin/crawler_main
COPY --from=builder /src/build/search_server /opt/avengine/bin/search_server
COPY --from=builder /src/build/index_builder /opt/avengine/bin/index_builder
COPY --from=builder /src/build/anchor_processor /opt/avengine/bin/anchor_processor
COPY --from=builder /src/deploy/start-crawler-swarm.sh /opt/avengine/bin/start-crawler-swarm.sh
COPY --from=builder /src/deploy/stop-crawler-swarm.sh /opt/avengine/bin/stop-crawler-swarm.sh
COPY --from=builder /src/deploy/aggregate-corpus-index.sh /opt/avengine/bin/aggregate-corpus-index.sh
COPY --from=builder /src/seed_list.txt /opt/avengine/config/seed_list.txt
COPY --from=builder /src/banned_urls.txt /opt/avengine/config/banned_urls.txt
COPY --from=builder /src/banned_words.txt /opt/avengine/config/banned_words.txt
COPY --from=builder /src/profiling/scripts/profile_query_pipeline.sh /opt/avengine/profiling/scripts/profile_query_pipeline.sh
COPY --from=builder /src/profiling/scripts/run_search_http_profile.py /opt/avengine/profiling/scripts/run_search_http_profile.py
COPY --from=builder /src/profiling/scripts/query_fixture.py /opt/avengine/profiling/scripts/query_fixture.py
COPY --from=builder /src/profiling/fixtures/search_corpus.tsv /opt/avengine/profiling/fixtures/search_corpus.tsv
COPY --from=builder /src/profiling/fixtures/search_queries.tsv /opt/avengine/profiling/fixtures/search_queries.tsv
COPY deploy/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh /opt/avengine/bin/start-crawler-swarm.sh \
    /opt/avengine/bin/stop-crawler-swarm.sh /opt/avengine/bin/aggregate-corpus-index.sh \
    /opt/avengine/profiling/scripts/profile_query_pipeline.sh \
    && printf 'export PATH=/opt/avengine/bin:$PATH\n' > /etc/profile.d/avengine-path.sh \
    && chown -R engine:engine /opt/avengine /var/lib/avengine

EXPOSE 22
CMD ["/usr/local/bin/entrypoint.sh"]
