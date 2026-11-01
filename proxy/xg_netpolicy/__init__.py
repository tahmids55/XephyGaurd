"""XephyGuard network policy: shared logic for the DNS, gateway (nftables) and proxy layers.

The CCC (Laravel) writes one JSON *state* document whenever sessions, modes, per-PC overrides or
blocklists change. Everything on the network side is derived from that document:

    state.json --(generate.py)--> unbound views, nftables set elements, proxy.json
                                   \\-> mitmproxy addon (hot-reloads proxy.json)
"""
__all__ = ["domains", "generate", "state"]
