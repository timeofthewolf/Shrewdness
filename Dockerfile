FROM node:22-alpine AS web
WORKDIR /web
COPY web/package.json web/package-lock.json ./
RUN npm ci
COPY web/ ./
RUN npm run build

FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ cmake make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY src/ src/
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target shrewdness shrewdc -j"$(nproc)"

FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /src/build/shrewdness /src/build/shrewdc /app/
COPY --from=web /web/dist /app/web
COPY examples/ /app/examples
ENV SHREWDNESS_WEB=/app/web SHREWDNESS_EXAMPLES=/app/examples

RUN useradd --system --uid 10001 --no-create-home --shell /usr/sbin/nologin \
    shrewdness
USER 10001

EXPOSE 7070
CMD ["/app/shrewdness", "--net", "--port", "7070", "--public"]

