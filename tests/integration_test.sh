#!/bin/sh

# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:

# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above
#   copyright notice, this list of conditions and the following disclaimer
#   in the documentation and/or other materials provided with the
#   distribution.
# * Neither the name of the  nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.



cleanup() 
{
	trap - TERM INT
	# set +eo pipefail
	for j in $(jobs -p)
	do
		echo "Stopping $j"
		kill $j
		wait $j
	done
}

tail_log() 
{
	tail -n +0 -F $1 || true
}

ELECTRUM_PATH="electrum"
if [ -f "tests/testdeps/electrum" ]; then
    ELECTRUM_PATH="tests/testdeps/electrum"
fi

BITCOIN_CLI_PATH="bitcoin-cli"
if [ -f "tests/testdeps/bitcoin/bin/bitcoin-cli" ]; then
    BITCOIN_CLI_PATH="tests/testdeps/bitcoin/bin/bitcoin-cli"
fi

BITCOIND_PATH="bitcoind"
if [ -f "tests/testdeps/bitcoin/bin/bitcoind" ]; then
    BITCOIND_PATH="tests/testdeps/bitcoin/bin/bitcoind"
fi

ELECTRUMD_PATH="./electrumd"
CONF_PATH="tests/electrumd_test.conf"
TEST_DATA_DIR="tests/test_data"

BITCOIN_DATA_DIR=$TEST_DATA_DIR/bitcoin
ELECTRUM_DATA_DIR=$TEST_DATA_DIR/electrum
ELECTRUMD_DATA_DIR=$TEST_DATA_DIR/electrumd_test_db

rm -rf $TEST_DATA_DIR
mkdir -p $BITCOIN_DATA_DIR
mkdir -p $ELECTRUM_DATA_DIR
mkdir -p $ELECTRUMD_DATA_DIR

trap cleanup INT TERM EXIT

BTC="$BITCOIN_CLI_PATH -regtest -datadir=$BITCOIN_DATA_DIR"
ELECTRUM="$ELECTRUM_PATH --regtest"
EL="$ELECTRUM --wallet=$ELECTRUM_DATA_DIR/wallet"

echo "[*] starting $($BITCOIND_PATH -version | head -n1)..."
$BITCOIND_PATH -txindex -regtest -debug=all -datadir=$BITCOIN_DATA_DIR -printtoconsole=0 &
BITCOIND_PID=$!

$BTC -rpcwait getblockcount > /dev/null

echo "[*] creating Electrum `$ELECTRUM_PATH version --offline` wallet..."
WALLET=`$EL --offline create --seed_type=segwit`
MINING_ADDR=`$EL --offline getunusedaddress`

$BTC generatetoaddress 110 $MINING_ADDR > /dev/null
echo `$BTC getblockchaininfo | jq -r '"[*] generated \(.blocks) regtest blocks (\(.size_on_disk/1e3) kB)"'` to $MINING_ADDR

TIP=`$BTC getbestblockhash`

$ELECTRUMD_PATH -c $CONF_PATH -ldebug -nregtest >> $ELECTRUMD_DATA_DIR/test.log 2>&1 &
ELECTRUMD_PID=$!
tail_log $ELECTRUMD_DATA_DIR/test.log | grep -m1 "electrum rpc server: socket is listening"

echo "[*] starting electrum wallet daemon "
$ELECTRUM daemon --server localhost:60401:t -1 -vDEBUG 2> $ELECTRUM_DATA_DIR/regtest-debug.log &
ELECTRUM_PID=$!
tail_log $ELECTRUM_DATA_DIR/regtest-debug.log | grep -m1 "connection established"
echo "[*] wallet info: $($EL getinfo)"

echo "[*] loading Electrum wallet..."
$EL load_wallet

json_result=$($EL getbalance)
echo "[*] getbalance JSON: $json_result"
test "`echo $json_result | jq -c .`" = '{"confirmed":"550","unmatured":"4950"}'

NEW_ADDR=`$EL getunusedaddress`
echo "[*] getunusedaddress: $NEW_ADDR"

echo "[*] payto & broadcast"
TXID=$($EL broadcast $($EL payto $NEW_ADDR 123 --fee 0.001 --password=''))

json_result=$($EL get_tx_status $TXID )
echo "[*] get_tx_status JSON: $json_result"
test "`echo $json_result | jq -c .`" = '{"confirmations":0}'

json_result=$($EL getaddresshistory $NEW_ADDR)
echo "[*] getaddresshistory JSON: $json_result"
test "`echo $json_result | jq -c .`" = "[{\"fee\":100000,\"height\":0,\"tx_hash\":\"$TXID\"}]"

json_result=$($EL getbalance)
echo "[*] getbalance JSON: $json_result"
test "`echo $json_result | jq -c .`" = '{"confirmed":"549.999","unmatured":"4950"}'

echo "[*] generating bitcoin block"
$BTC generatetoaddress 1 $MINING_ADDR > /dev/null
$BTC getblockcount > /dev/null

echo "[*] wait for new block -> takes about ~ 5min"
# kill -USR1 $ELECTRUMD_PID  # notify server to index new block
tail_log $ELECTRUM_DATA_DIR/regtest-debug.log | grep -m1 "verified $TXID" > /dev/null

json_result=$($EL get_tx_status $TXID)
echo "[*] get_tx_status JSON: $json_result"
test "`echo $json_result | jq -c .`" = '{"confirmations":1}'

json_result=$($EL getaddresshistory $NEW_ADDR)
echo "[*] getaddresshistory JSON: $json_result"
test "`echo $json_result | jq -c .`" = "[{\"height\":111,\"tx_hash\":\"$TXID\"}]"

json_result=$($EL getbalance)
echo "[*] getbalance JSON: $json_result"
test "`echo $json_result | jq -c .`" = '{"confirmed":"599.999","unmatured":"4950.001"}'

echo "[*] electrum `$EL stop`"  # disconnect wallet
wait $ELECTRUM_PID

kill -INT $ELECTRUMD_PID  # close server
tail_log $ELECTRUMD_DATA_DIR/test.log | grep -m1 "electrumd: exited"
wait $ELECTRUMD_PID

$BTC stop # stop bitcoind
wait $BITCOIND_PID

echo "[*] test passed!"
