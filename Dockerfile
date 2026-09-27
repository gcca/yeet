# syntax=docker/dockerfile:1.7
ARG DEPS_IMAGE=yeet:deps
ARG STATIC_IMAGE=cgr.dev/chainguard/static:latest
ARG BUILD_JOBS=8

FROM ${DEPS_IMAGE} AS deps

FROM deps AS build

WORKDIR /src

COPY CMakeLists.txt dune-project ./
COPY 3rdparty ./3rdparty
COPY src ./src
COPY lib ./lib
COPY cmd ./cmd

ARG BUILD_JOBS

RUN cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel "${BUILD_JOBS}"

RUN eval $(opam env) \
    && dune build --profile=release @install \
    && dune install --profile=release --prefix=/out/usr/local \
        --bindir=/out/usr/local/bin yeet

RUN mkdir -p /out/usr/local/bin && cp build/bin/* /out/usr/local/bin/

FROM deps AS assemble

COPY --from=build /out/ /out/

RUN mkdir -p /out/app/data \
    && chown -R 65532:65532 /out/app \
    && cp "$(command -v tini)" /out/usr/local/bin/tini \
    && loader="$(find /usr/lib /lib -maxdepth 1 -name 'ld-linux-*.so.*' | head -n1)" \
    && for bin in /out/usr/local/bin/*; do \
         "$loader" --list "$bin" 2>/dev/null; \
       done \
       | sed -nE 's#^[[:space:]]*([^[:space:]]+)[[:space:]]=>[[:space:]]*(/[^[:space:]]+).*#\1|\2#p; s#^[[:space:]]*(/[^[:space:]]+)[[:space:]].*#\1|\1#p' \
       | sort -u > /tmp/libs.txt \
    && while IFS='|' read -r name path; do \
         real="$(realpath "$path")" \
         && mkdir -p "/out$(dirname "$real")" \
         && cp -Lf "$real" "/out$real" \
         && base="$(basename "$name")" \
         && [ "$base" = "$(basename "$real")" ] \
         || ln -sf "$(basename "$real")" "/out$(dirname "$real")/$base"; \
       done < /tmp/libs.txt \
    && cp -L "$loader" "/out$loader"

FROM ${STATIC_IMAGE} AS execute

COPY --from=assemble /out/ /

WORKDIR /app

ENV TZ=UTC \
    YEET_DB=/app/data/yeet.db

VOLUME /app/data

USER 65532:65532

HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD ["/usr/local/bin/yeet-list", "--all"]

ENTRYPOINT ["/usr/local/bin/tini", "--", "/usr/local/bin/yeet"]
CMD ["run"]
