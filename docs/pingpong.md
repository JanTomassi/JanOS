# Ping Pong App

`apps/pingpong` is a user-space IPC request/reply example.

The server creates an endpoint and prints its handle:

```
pingpong server
```

A client sends numbered ping requests and waits synchronously for each reply:

```
pingpong client <endpoint> [count]
```

The process manager must grant the client `JANOS_IPC_RIGHT_SEND` for the
server endpoint before starting it. The server retains receive and reply
rights. The protocol uses type `0x50494e47`, a four-byte sequence payload, and
the existing fixed-size IPC message ABI.

The host test validates 1,000 request/reply payloads and rejects a message with
conflicting request/reply flags. The app is compiled as a freestanding i386
user binary. The multi-process integration scenario is only included in the
dedicated pingpong test ISO; normal JanOS boots do not load or start it.
