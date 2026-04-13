# Electrumd

Electrumd is a small and lightweight Electrum server.

### Build Dependencies
- libcurl
- LevelDB
- OpenSSL

#### Test
- pytest
- ~~jq (required by the integration_test.sh script)~~ (Deprecated)

### Code Contribution Rules

- Any contribution is appreciated.
- Please use English in comments.

### Reporting an issue
- Try to provide as much informations as possible (e.g. OS, libs versions, height, Bitcoin CORE version ...)
- Use a debug log level and attach the debug file

### Things not working
- OpenBSD support coming soon...
- Peers Add / Subscribe
- SSL server not yet supported

### Build and install
- To build and install electrumd
```bash
make install clean
```

### Running tests
- Electrum RPC test:
```bash
pytest tests/test_electrum_rpc.py
```
    - requires sync electrumd daemon (at least at height 400000)

- Integration with electrum wallet test:
```bash
pytest tests/test_wallet_integration.py
```

### Additional notes
- For now at least, electrumd must be intended to be used in a small scale home or friends environment it is not meant for a enterprise service level
- Currencies different from BTC are not supported for now

### Irc channels
* #electrumd_irc on irc.darkscience.net
