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
- Use English for code comments, commit messages, PR titles, and PR descriptions.
- Create a branch for your changes.
- Open a Pull Request from that branch against the main branch.
- Run tests: if possible, run at least the _test_wallet_integration.py_ test before submitting the PR.

### Reporting an issue
- Try to provide as much informations as possible (e.g. OS type, OS version, libs versions, height, Bitcoin CORE version ...)
- Use a debug log level and attach the debug file

### Things not working
- Peering suport (Add / Subscribe)

### Build and install
- To build and install electrumd
```bash
make install clean
```

### Running tests
- Electrum RPC test:
```bash
pytest tests/test_electrum_rpc.py # requires a fully synced electrumd daemon
```

- Integration with electrum wallet test:
```bash
pytest tests/test_wallet_integration.py
```

### Additional notes
- Requires txindex
- For now at least, electrumd must be intended to be used in a small scale home or friends environment it is not meant for a enterprise service level
- Currencies different from BTC are not supported for now

### Irc channels
* #electrumd_irc on irc.darkscience.net
