# Go WebSocket screen-streaming relay for XephyGuard (port 8082).
FROM golang:bookworm AS builder
ENV GOTOOLCHAIN=auto
WORKDIR /src
COPY xephyguard-screen/gateway/go.mod xephyguard-screen/gateway/go.sum ./
RUN go mod download
COPY xephyguard-screen/gateway/*.go ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/xephyguard-screen-gateway .

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=builder /out/xephyguard-screen-gateway /usr/local/bin/xephyguard-screen-gateway
USER nobody
CMD ["/usr/local/bin/xephyguard-screen-gateway"]
