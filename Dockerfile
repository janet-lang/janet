FROM debian:10.4-slim as build

RUN DEBIAN_FRONTEND=noninteractive apt-get update --yes \
    && DEBIAN_FRONTEND=noninteractive apt-get install --yes --no-install-recommends \
        gcc \
        libc6-dev \
        git \
        make \
    && rm -fr /var/lib/apt/lists/*

RUN ["useradd", "-m", "janet"]
RUN mkdir /home/janet/janet-src \
    && chown janet:janet /home/janet/janet-src
USER janet
WORKDIR /home/janet/janet-src

COPY --chown=janet:janet . .

RUN ["make"]
RUN ["make", "test"]


FROM debian:10.4-slim

RUN ["useradd", "-m", "janet"]
USER janet
WORKDIR /home/janet

COPY --from=build /home/janet/janet-src/build/janet /usr/local/bin/

ENTRYPOINT /usr/local/bin/janet

