FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
  build-essential \
  cmake \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY avengine /src/avengine
RUN cmake -S /src/avengine -B /src/build -DCMAKE_BUILD_TYPE=Release \
  && cmake --build /src/build -j"$(nproc)"

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
  openssh-server \
  ca-certificates \
  && rm -rf /var/lib/apt/lists/* \
  && mkdir -p /var/run/sshd

RUN useradd -m -s /bin/bash engine
RUN mkdir -p /home/engine/.ssh && chown -R engine:engine /home/engine/.ssh && chmod 700 /home/engine/.ssh

COPY --from=builder /src/build/vector_demo /opt/avengine/bin/vector_demo
COPY avengine/deploy/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

EXPOSE 22
CMD ["/usr/local/bin/entrypoint.sh"]
