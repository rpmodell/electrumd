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

import json
import os
import shutil
import signal
import subprocess
import time
from time import sleep

ELECTRUMD_PATH = "./electrumd"
CONF_PATH = "tests/electrumd_test.conf"

TEST_DATA_DIR = "tests/test_data"
TESTDEPS_PATH = "tests/testdeps/"


# fixme revise wait log message function
def wait_log_message(path, message, timeout=30):
    start_time = time.time()
    while (time.time() - start_time) < timeout:
        if os.path.exists(path):
            break

        sleep(0.5)

    assert os.path.exists(path), f'File not found {path} timeout reached'

    with open(path, 'r') as fp:
        while (time.time() - start_time) < timeout:
            line = fp.readline()
            if line is not None and message in line.strip():
                return

    assert False, f'Wait message "{message}" from file {path} timeout reached'


def run_detached_process(args: [], err_fpath=None):
    err_fileno = subprocess.STDOUT
    if err_fpath is not None:
        err_fileno = open(err_fpath, 'wb')

    return subprocess.Popen(args, start_new_session=True, stderr=err_fileno)


def bitcoin_cli(exec_path, data_dir, more_args: []):
    args = [exec_path, '-regtest', f'-datadir={data_dir}'] + more_args
    proc = subprocess.run(args, stdout=subprocess.PIPE)
    assert proc.returncode == 0, f'Error running [{" ".join(args)}] {proc.stderr}'
    return proc.stdout.strip()


def electrum(exec_path, data_dir, more_args: []):
    args = [exec_path, '--regtest', f'--wallet={data_dir}/wallet'] + more_args
    proc = subprocess.run(args, stdout=subprocess.PIPE)
    assert proc.returncode == 0, f'Error running [{" ".join(args)}] {proc.stderr}'
    pso = proc.stdout
    return pso.decode('utf-8').strip()


def test_wallet_integration():
    bitcoind_path = f'{TESTDEPS_PATH}/bitcoin/bin/bitcoind'
    bitcoin_cli_path = f'{TESTDEPS_PATH}/bitcoin/bin/bitcoin-cli'
    electrum_path = f'{TESTDEPS_PATH}/electrum'
    electrumd_path = ELECTRUMD_PATH

    assert os.path.exists(bitcoind_path)
    assert os.path.exists(bitcoin_cli_path)
    assert os.path.exists(electrum_path)

    test_data_dir = TEST_DATA_DIR
    bitcoin_data_dir = f'{test_data_dir}/bitcoin/'
    electrum_data_dir = f'{test_data_dir}/electrum/'
    electrumd_data_dir = f'{test_data_dir}/electrumd_test_db/'
    electrum_log_path = f'{electrum_data_dir}/regtest-debug.log'
    electrumd_log_path = f'{electrumd_data_dir}/test.log'
    electrumd_fatal_log_path = f'{electrumd_data_dir}/fatal.log'

    if os.path.exists(test_data_dir):
        shutil.rmtree(test_data_dir, ignore_errors=True)

    os.mkdir(test_data_dir)
    os.mkdir(bitcoin_data_dir)
    os.mkdir(electrum_data_dir)
    os.mkdir(electrumd_data_dir)

    print("[*] starting $($BITCOIND_PATH -version | head -n1)...")
    # $BITCOIND_PATH - txindex - regtest - debug = all - datadir =$BITCOIN_DATA_DIR - printtoconsole = 0 &
    # BITCOIND_PID =$!
    bitcoind_proc = run_detached_process(
        [bitcoind_path, '-txindex', '-regtest', '-debug=all', f'-datadir={bitcoin_data_dir}', '-printtoconsole=0']
    )

    # $BTC - rpcwait getblockcount > / dev / null
    bitcoin_cli(bitcoin_cli_path, bitcoin_data_dir, ['-rpcwait', 'getblockcount'])

    print("[*] creating Electrum `$ELECTRUM_PATH version --offline` wallet...")
    wallet = electrum(electrum_path, electrum_data_dir, ['--offline', 'create', '--seed_type=segwit'])

    # MINING_ADDR = `$EL - -offline getunusedaddress`
    mining_addr = electrum(electrum_path, electrum_data_dir, ['--offline', 'getunusedaddress'])

    # $BTC generatetoaddress 110 $MINING_ADDR > / dev / null
    bitcoin_cli(bitcoin_cli_path, bitcoin_data_dir, ['generatetoaddress', '110', mining_addr])

    json_result = json.loads(bitcoin_cli(bitcoin_cli_path, bitcoin_data_dir, ['getblockchaininfo']))
    print(f"[*] generated {json_result['blocks']} regtest blocks {int(json_result['size_on_disk']) / 1e3} kB)")
    # echo`$BTCgetblockchaininfo | jq - r '"[*] generated \(.blocks) regtest blocks (\(.size_on_disk/1e3) kB)"'` to
    # $MINING_ADDR

    # TIP = `$BTC
    # getbestblockhash
    # `

    # $ELECTRUMD_PATH - c $CONF_PATH - ldebug - nregtest 2 > $ELECTRUMD_DATA_DIR / fatal.log &
    # ELECTRUMD_PID =$!
    electrumd_proc = run_detached_process(
        [electrumd_path, '-c', f'{CONF_PATH}', '-l', 'debug'],
        electrumd_fatal_log_path
    )

    # tail_log $ELECTRUMD_DATA_DIR / test.log | grep - m1
    # "electrum rpc server: socket is listening"
    wait_log_message(electrumd_log_path, "electrum rpc server: socket is listening")

    print("[*] starting electrum wallet daemon ")
    # $ELECTRUM daemon --server localhost: 60401:t -1 -vDEBUG 2 > $ELECTRUM_DATA_DIR / regtest - debug.log &
    electrum_proc = run_detached_process(
        [electrum_path, '--regtest', 'daemon', '--server', 'localhost:60401:t', '-1', '-vDEBUG'],
        electrum_log_path
    )

    # tail_log $ELECTRUM_DATA_DIR / regtest - debug.log | grep - m1 "connection established"
    wait_log_message(electrum_log_path, "connection established")

    json_result = json.loads(electrum(electrum_path, electrum_data_dir, ['getinfo']))
    print(f"[*] wallet info: {json_result}")

    print("[*] loading Electrum wallet...")
    # $EL load_wallet
    electrum(electrum_path, electrum_data_dir, ['load_wallet'])

    json_result = json.loads(electrum(electrum_path, electrum_data_dir, ['getbalance']))
    print(f"[*] getbalance JSON: {json_result}")

    # test "`echo $json_result | jq -c .`" = '{"confirmed":"550","unmatured":"4950"}'
    assert json_result == {
        "confirmed": "550",
        "unmatured": "4950"
    }

    # NEW_ADDR = `$EL getunusedaddress`
    new_addr = electrum(electrum_path, electrum_data_dir, ['getunusedaddress'])
    print(f"[*] getunusedaddress: {new_addr}")

    print("[*] payto & broadcast")
    # $($EL payto $NEW_ADDR 123 --fee 0.001 --password='')
    raw_tx = electrum(electrum_path, electrum_data_dir,
                      ['payto', new_addr, '123', '--fee', '0.001', '--password=']  # problem with password=""
                      )

    # txid =$($EL broadcast )
    tx_id = electrum(electrum_path, electrum_data_dir, ['broadcast', raw_tx])

    # json_result =$($EL get_tx_status $TXID)
    json_result = json.loads(electrum(electrum_path, electrum_data_dir, ['get_tx_status', tx_id]))
    print(f"[*] get_tx_status JSON: {json_result}")
    # test"`echo $json_result | jq -c .`" = '{"confirmations":0}'
    assert json_result == {
        "confirmations": 0
    }

    # json_result =$($EL getaddresshistory $NEW_ADDR)
    json_result = json.loads(electrum(electrum_path, electrum_data_dir, ['getaddresshistory', new_addr]))
    print(f"[*] getaddresshistory JSON: {json_result}")
    # test "`echo $json_result | jq -c .`" = "[{\"fee\":100000,\"height\":0,\"tx_hash\":\"$TXID\"}]"
    assert json_result == [
        {
            "fee": 100000,
            "height": 0,
            "tx_hash": tx_id
        }
    ]

    # json_result =$($EL getbalance)
    json_result = json.loads(electrum(electrum_path, electrum_data_dir, ['getbalance']))
    print(f"[*] getbalance JSON: {json_result}")
    # test "`echo $json_result | jq -c .`" = '{"confirmed":"549.999","unmatured":"4950"}'
    assert json_result == {
        "confirmed": "549.999",
        "unmatured": "4950"
    }

    print("[*] generating bitcoin block")
    # $BTC generatetoaddress 1 $MINING_ADDR > / dev / null
    bitcoin_cli(bitcoin_cli_path, bitcoin_data_dir, ['generatetoaddress', '1', mining_addr])

    # $BTC getblockcount > / dev / null
    bitcoin_cli(bitcoin_cli_path, bitcoin_data_dir, ['getblockcount'])

    print("[*] wait for new block -> takes about ~ 5min")
    # kill -USR1 $ELECTRUMD_PID  # notify server to index new block
    # tail_log $ELECTRUM_DATA_DIR / regtest - debug.log | grep - m1 "verified $TXID" > / dev / null
    wait_log_message(electrum_log_path, f'verified {tx_id}', timeout=60)

    # json_result =$($EL get_tx_status $TXID)
    json_result = json.loads(electrum(electrum_path, electrum_data_dir, ['get_tx_status', tx_id]))
    print(f"[*] get_tx_status JSON: {json_result}")
    # test "`echo $json_result | jq -c .`" = '{"confirmations":1}'
    assert json_result == {
        "confirmations": 1
    }

    # json_result =$($EL getaddresshistory $NEW_ADDR)
    json_result = json.loads(electrum(electrum_path, electrum_data_dir, ['getaddresshistory', new_addr]))
    print(f"[*] getaddresshistory JSON: {json_result}")
    # test "`echo $json_result | jq -c .`" = "[{\"height\":111,\"tx_hash\":\"$TXID\"}]"
    assert json_result == [
        {
            "height": 111,
            "tx_hash": tx_id
        }
    ]

    # json_result =$($EL getbalance)
    json_result = json.loads(electrum(electrum_path, electrum_data_dir, ['getbalance']))
    print(f"[*] getbalance JSON: {json_result}")
    # test "`echo $json_result | jq -c .`" = '{"confirmed":"599.999","unmatured":"4950.001"}'
    assert json_result == {
        "confirmed": "599.999",
        "unmatured": "4950.001"
    }

    json_result = electrum(electrum_path, electrum_data_dir, ['stop'])
    print(f"[*] electrum {json_result}")  # disconnect wallet
    # wait $ELECTRUM_PID
    electrum_proc.send_signal(signal.SIGINT)
    electrum_proc.wait()

    # kill - INT $ELECTRUMD_PID  # close server
    # tail_log $ELECTRUMD_DATA_DIR / test.log | grep - m1
    # "electrumd: exited"
    # wait $ELECTRUMD_PID
    electrumd_proc.send_signal(signal.SIGINT)
    electrumd_proc.wait()

    # $BTC stop  # stop bitcoind
    # wait $BITCOIND_PID
    bitcoin_cli(bitcoin_cli_path, bitcoin_data_dir, ['getblockcount'])
    bitcoind_proc.send_signal(signal.SIGINT)
    bitcoind_proc.wait()

    print("[*] test passed!")
