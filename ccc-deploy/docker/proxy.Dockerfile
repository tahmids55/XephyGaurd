FROM mitmproxy/mitmproxy:12
# Dedicated uid/gid 1500 (owner of the CA bundle on the host, see install.sh).
RUN groupmod -o -g 1500 mitmproxy && usermod -o -u 1500 -g 1500 mitmproxy && chown -R 1500:1500 /home/mitmproxy
COPY proxy/xg_netpolicy /opt/xephyguard/proxy/xg_netpolicy
COPY proxy/addons /opt/xephyguard/proxy/addons
# The lab CA (key + cert) is mounted at the default confdir /home/mitmproxy/.mitmproxy as
# mitmproxy-ca.pem (0600, owned by uid 1500). The image entrypoint then runs mitmproxy as that uid.
CMD ["mitmdump", "--mode", "transparent", "--listen-host", "0.0.0.0", "--listen-port", "8080", "--showhost", \
     "--set", "block_global=false", \
     "--set", "connection_strategy=lazy", "--set", "termlog_verbosity=warn", \
     "-s", "/opt/xephyguard/proxy/addons/xephyguard_addon.py", "--set", "xg_policy=/var/lib/xephyguard/net/proxy.json"]
