# p007 net integration: trajectory echo

This is a **two-process** integration test intended to run across hosts.

## Build

```bash
cd /home/kmd/rust-rpg

gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude \
  tests/p007_net_integration_server_tests.c src/net/udp/*.c \
  -o build/p007_net_integration_server

gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude \
  tests/p007_net_integration_client_tests.c src/net/udp/*.c -lm \
  -o build/p007_net_integration_client
```

## Run

On the server host:

```bash
./build/p007_net_integration_server 40000
```

On the client host:

```bash
./build/p007_net_integration_client <server_ipv4> 40000
```

The client exits non-zero if it receives too few echoes or if quantization/serialization roundtrip error exceeds thresholds.
