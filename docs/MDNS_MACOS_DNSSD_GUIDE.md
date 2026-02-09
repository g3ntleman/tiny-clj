# macOS Interactive mDNS/DNS-SD Test Guide (using `dns-sd`)

This guide is an **interactive** (manual) integration test for `tiny-clj.net.mdns` on macOS.

It uses macOS' built-in `dns-sd` tool as a responder/advertiser and the Tiny-CLJ REPL as a browser/resolver.

## What this validates

- `tiny-clj.net.mdns/open` returns a native handle (no work happens before this call).
- `tiny-clj.net.mdns/browse!` starts browsing and delivers events via `tiny-clj.net.mdns/on-event`.

On macOS, Tiny-CLJ prefers Apple's DNS-SD API (mDNSResponder) for correctness across interfaces.
You can force the legacy raw-UDP path via `TINYCLJ_MDNS_USE_DNSSD=0` for debugging.

## Prerequisites

- A **host build** of Tiny-CLJ:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -B build
cmake --build build
```

- `dns-sd` is available on macOS by default:

```bash
which dns-sd
```

## Terminal 1: Start a DNS-SD responder with `dns-sd -R`

We advertise a `_matterc._udp` service (Matter commissionable discovery).

Pick a port that isn't in use (example: `5540`). The instance name can be anything.

```bash
dns-sd -R "tiny-clj-mdns-test" _matterc._udp local 5540 D=3840 VP=123+456 CM=1
```

Notes:
- `D=... VP=... CM=...` are **TXT** key/value pairs (they are treated as opaque bytes by Tiny-CLJ currently).
- Keep this running while you browse/resolve from Tiny-CLJ.

You should see `dns-sd` print that the service is being registered.

## Terminal 2: Start Tiny-CLJ REPL and browse

Start the REPL:

```bash
./build/tiny-clj-repl
```

Optional: force raw-UDP backend (legacy) instead of DNS-SD:

```bash
TINYCLJ_MDNS_USE_DNSSD=0 ./build/tiny-clj-repl
```

Then paste the following forms:

```clojure
(require 'tiny-clj.net.mdns)

(def h (tiny-clj.net.mdns/open))

(tiny-clj.net.mdns/on-event h
  (fn [ev]
    ;; ev is a map; print it so we can observe resolution behavior.
    (println ev)))

(tiny-clj.net.mdns/browse! h "_matterc._udp.local")
```

## Expected events

You should see one or more maps printed.

The event map fields are:
- `:type` one of `:instance-found`, `:resolved`, `:expired`
- `:instance` instance name (string)
- `:service` service name (string)
- On `:resolved` additionally:
  - `:host` host name (string)
  - `:port` port (integer)
  - `:addrs` vector of address strings (IPv4/IPv6)
  - `:txt` byte-array (raw TXT blob)

Example shapes (values will differ):

```clojure
{:type :instance-found
 :instance "tiny-clj-mdns-test"
 :service "_matterc._udp.local"}
```

```clojure
{:type :resolved
 :instance "tiny-clj-mdns-test"
 :service "_matterc._udp.local"
 :host "my-mac.local"
 :port 5540
 :addrs ["192.168.1.10" "fe80::...."]
 :txt #byte-array[...] }
```

## Cleanup

Stop browsing and release resources:

```clojure
(tiny-clj.net.mdns/close! h)
```

Then stop `dns-sd` in Terminal 1 (`Ctrl+C`).

## Troubleshooting

### No events show up

- Ensure the service string includes `.local`:
  - Correct: `"_matterc._udp.local"`
  - Wrong: `"_matterc._udp"`

- Ensure the `dns-sd -R ...` command is still running.

- Try browsing with `dns-sd` itself to confirm the service exists on the host:

```bash
dns-sd -B _matterc._udp local
```

### `mdns/open` fails on your machine

`platform_mdns_open` may fail if the environment restricts certain socket options (sandboxing, corporate security tooling, etc.).

If this happens, try running the REPL **outside** of restrictive environments and ensure your firewall allows local network traffic.

### Extra debugging

You can enable a low-level receive trace in the raw-UDP backend:

```bash
TINYCLJ_MDNS_TRACE=1 TINYCLJ_MDNS_USE_DNSSD=0 ./build/tiny-clj-repl
```

