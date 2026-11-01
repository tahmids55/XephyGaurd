# Unbound resolver + nftables set updater, driven by the CCC's policy state document.
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends unbound nftables python3 ca-certificates dnsutils \
    && rm -rf /var/lib/apt/lists/*
COPY proxy/xg_netpolicy /opt/xephyguard/proxy/xg_netpolicy
COPY proxy/dns/unbound.conf /etc/unbound/unbound.conf
COPY ccc-deploy/docker/netpolicy-run.sh /usr/local/bin/netpolicy-run
RUN chmod +x /usr/local/bin/netpolicy-run && unbound-anchor -a /var/lib/unbound/root.key || true
CMD ["netpolicy-run"]
