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
import signal
import socket
import time
import pytest
from subprocess import Popen, PIPE

ELECTRUMD_PATH = "./electrumd"
CONF_PATH = "tests/electrumd_test.conf"

ELECTRUMD_HOST = "127.0.0.1"
ELECTRUMD_PORT = 50001

# ELECTRUMD_HOST = "electrum1.bluewallet.io"
# ELECTRUMD_PORT = 50001

electrumd_process: Popen = None


@pytest.fixture
def setup_electrumd_daemon(session):
    # def teardown_electrumd_daemon(session):
    #     electrumd_process.send_signal(signal.CTRL_C_EVENT)

    global electrumd_process
    if electrumd_process != None:
        return

    electrumd_process = Popen([ELECTRUMD_PATH, "-c", CONF_PATH, "-l", "debug"], stdout=PIPE, stderr=PIPE,
                              start_new_session=True)
    time.sleep(1)

    yield

    electrumd_process.send_signal(signal.CTRL_C_EVENT)


def assert_electrumd_running():
    pass
    # assert electrumd_process != None
    #
    # poll = electrumd_process.poll()
    # if poll != None:
    #     if electrumd_process.returncode != 0:
    #         raise Exception(electrumd_process.stderr.read())


def jsonrpc_send_request(host: str, port: int, method: str, params: list):
    request = {
        "jsonrpc": "2.0",
        "id": os.urandom(6).hex(),
        "method": method,
        "params": params
    }

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))

        s.sendall((json.dumps(request) + "\n").encode("utf-8"))

        data = b""
        while True:
            buf = s.recv(4096)
            data += buf
            if len(buf) < 4096:
                break

        s.close()
        response = json.loads(data.decode("utf-8").replace("\0", "").strip())
        if response["id"] != request["id"]:
            raise Exception("json rpc ex: id mismatch")

        if "error" in response:
            err_dict = response["error"]
            raise Exception(err_dict["message"], err_dict["code"])

        return response["result"]

    return {}


def test_server_version():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "server.version",
        ["electrum_client", ["1.4", "1.4.3"]]
    )

    print(result)
    assert result[0] == "electrumd"
    assert result[1] == "1.4"


def test_server_features():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "server.features",
        []
    )

    assert result["hosts"]["127.0.0.1"]["tcp_port"] == 50001
    assert result["hosts"]["127.0.0.1"]["ssl_port"] == None
    assert result["pruning"] == None
    assert result["server_version"] == "electrumd" 
    assert result["protocol_min"] == "1.4"
    assert result["protocol_max"] == "1.4"
    assert result["genesis_hash"] == "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"
    assert result["hash_function"] == "sha256"


def test_blockchain_block_headers_1():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.block.headers",
        [34889, 12]
    )

    assert result["hex"] == (
        "01000000e34cb7a4b592500e169dc6b7586a1409bfdfeecf7a47197dd22ca1660000000037cc503acaa1e2b36"
        "377f2d99c9050d0c37e3010f7d2beee6d421fa0fb7eaeb30601514b28c4001d2289ae11010000000c5d99ec17"
        "da67a526cca04d1413e76049359c7de43e2c56f43d10850000000092277c63fbbf21450e906cbb6f79450946b"
        "391e26ac527ad93aa819b5278a60d3404514b28c4001d1ed8c21101000000cc8de4cc2705826dee7193a31cca"
        "e2a3a6463a2f332cfc195a42e621000000009d216760a3167d6308b8c1e38aefd35a42faf83bd3f082006f232"
        "1c01220afdb6f05514b28c4001d13f1b09901000000f471f09125255ee156908a2d5fdf391abf878c421539cb"
        "4d853d4e2f00000000652b7c5bab141db59f656d78fe426f13ad2d358c8df36cd783be002a3927ac2af906514"
        "b28c4001d3facca0701000000d3fb6d6061aa947051301843f87d3c51aba4e6664d3384e76a1e629f00000000"
        "9f4b5419fa61d154da43e8113b2343596ab3d3087a3152a94cdee25f00a0b1d01a08514b28c4001d96bcf0050"
        "1000000c3a37a46f47adfedc403bf2283af7166dc13d6c9d9afab74248850a600000000d72bedec811b7d4a5d"
        "45e5c08614eb88d8d9d16d8afb46615b71a1c590d19192dc0b514b28c4001d32d3191501000000030b961f17d"
        "f82b9ac88fa0c0c147888af332b87eb86687f976ff23100000000b1aa76f7d6acbf86ae7853946bd6d1dbfe25"
        "5a702e36f20ca4ea5332eece7b879f0c514b28c4001df5301503010000009fca6c3bbc2eba058def6c3937882"
        "5e4681046a698fd13df4a22988e000000003b15498b1357103ec11fb33f8531a07f2ce102fe642add9ac36992"
        "f7f18b14c2510f514b28c4001d1bd3881001000000348c60e979f9e1cac434d302f474d73edfdf61c19bf8579"
        "c2375609000000000ef0764b16914b0c8aa8e4479402e720af0207187ba3879d42b5e3a00ce18e9acf410514b"
        "28c4001d38885508010000008a04f6f6e3e351d4f24dba1f078709e42939400e945a6eb0f2c4b23200000000a"
        "92e6379aca22cf4d42318af3a75435d566da22b296f8cfde441b4005cc325b06012514b28c4001d00e8800401"
        "000000ee54b4bfddaa31620632703f9776723b77295a0ded5de438a58cd95e0000000040a27fd6c81df274e31"
        "f7ffb134cb79b66d81f281a19fab4f3c533d62686aaf09915514b28c4001d7c276b1301000000351ec5e88bb0"
        "51ec70f5e4291b16e60fb583f8f78c4691fd1b00cb02000000001262f363537de6721216cd3eb02176e0b9fb5"
        "abff7b1ff22f50bbaae1a809da76917514b28c4001d6097e909"
    )
    assert result["count"] == 12
    assert result["max"] == 2016


def test_blockchain_block_headers_2():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.block.headers",
        [67988, 2]
    )

    assert result["hex"] == (
        "010000002f7af3aa53bd428e5807ce755bdc3d64ddb1d662b9a194deab86940400000000f615fc77db61e6ece"
        "0f6b6de7cecd06ac6615b684b88f0eaa640188c7d5940793b603f4cf4a3051c2043510001000000cda821097f"
        "270390edbc5cfb24131a506fdd3e846c3eee2a52f3a90300000000b448bfa7e99421baa3fd4fdccacf9fb20af"
        "4f3af422f27944e2588b6a4c483a677603f4cf4a3051c64120a02"
    )
    assert result["count"] == 2
    assert result["max"] == 2016


def test_blockchain_scripthash_getbalance_1():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.scripthash.get_balance",
        ["898339e1839b2ed57690385daa4b2ec8463982797ef5f666c3f78db21c6db227"]
    )

    assert result["confirmed"] == 0
    assert result["unconfirmed"] == 0


def test_blockchain_scripthash_getbalance_2():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.scripthash.get_balance",
        ["7726432279d1ca8969cdff7c4fe38abe17b0b551ac8bd134dd1dc1caf1a0c078"]
    )

    assert result["confirmed"] == 0
    assert result["unconfirmed"] == 0


def test_blockchain_scripthash_listunspent():
    assert_electrumd_running()

    # height = 348999

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.scripthash.listunspent",
        ["0cbd3eda5f01eb84f871d5e21d2cf624d7834c02ffabca803f4f5d68c3c85f66"]
    )

    assert len(result) == 0


def test_blockchain_scripthash_listunspent_2():
    assert_electrumd_running()

    # height = 348999

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.scripthash.listunspent",
        ["5f6046700aa7b611ecc5c22943d728b5cae1bf80a2aa98a4bf0b794e2c11762b"]
    )

    assert len(result) == 0


def test_blockchain_scripthash_subscribe_1():
    assert_electrumd_running()

    # height = 348999

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.scripthash.subscribe",
        ["88e3c420cb1d591ad3af9a585e17824766029e1530091ece293c0bea8c743260"]
    )

    assert result == "a78932f853466e372359ae731842fe7a872431ecf007e74ae2ad11d39aa7df7f"


def test_blockchain_scripthash_subscribe_2():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.scripthash.subscribe",
        ["0cbd3eda5f01eb84f871d5e21d2cf624d7834c02ffabca803f4f5d68c3c85f66"]
    )

    assert result == "98b01a04868ad41d2e5ee53bfed18aaa52594f586ff72d7cc5995b11b9ef93d2"


def test_blockchain_scripthash_subscribe_3():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.scripthash.subscribe",
        ["5f6046700aa7b611ecc5c22943d728b5cae1bf80a2aa98a4bf0b794e2c11762b"]
    )

    assert result == "994ab87f1729b1c40c94b9ebaea709df2938ce7d0679dc24490a973f6e1f3271"


def test_blockchain_scripthash_get_history_1():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.scripthash.get_history",
        ["88e3c420cb1d591ad3af9a585e17824766029e1530091ece293c0bea8c743260"]
    )

    assert len(result) == 2
    assert result[0]["tx_hash"] == "df41ce734c49b26893f4e25fb7e397cb624233028ede8bc91a6ab40b0d38d4e3"
    assert result[0]["height"] == 145888
    assert result[1]["tx_hash"] == "394413d7afd2d5708acd2872d13dbe6b74fffb52bde94e69a63010e9d154dce7"
    assert result[1]["height"] == 145891


def test_blockchain_scripthash_get_history_2():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.scripthash.get_history",
        ["2066d334854ac44fd8a7e5aac028aad69a24541dd95ce309fc419506869f9e32"]
    )

    assert len(result) == 16
    assert result[0]["tx_hash"] == "04ee5915706de3a01ebd738f2056791a3faa45c07c7dcfee716c0c04289cd2b9"
    assert result[0]["height"] == 281942
    assert result[1]["tx_hash"] == "30dfd489a48dfc1a2c2cbc6f5a3e8d645c7cc18b599234f83656490c0ccc3f02"
    assert result[1]["height"] == 282009
    assert result[2]["tx_hash"] == "b7bb5b262d2d119408585e9bb0557ae7de01ff90d3946d9c16f8accc8b376c58"
    assert result[2]["height"] == 282616
    assert result[3]["tx_hash"] == "6466ad542b1ca250df105ef52316b6a81b69c7f78eddd5fa6d7716d5bb3dfaef"
    assert result[3]["height"] == 282894
    assert result[4]["tx_hash"] == "aa5a4d8fb9a0a851bae66da709297d0f4f69fcc7ddae3c669a1ed7bd0582f0bc"
    assert result[4]["height"] == 282910
    assert result[5]["tx_hash"] == "6a53eb4f3e64c99b86b2f087b5cc08da7267adb66a0073142eb6d203aedfd13b"
    assert result[5]["height"] == 283205
    assert result[6]["tx_hash"] == "b0f9747d879a5f7976329b7c8013c602ee2ed203ae8f62d9c04e329f4eab56f6"
    assert result[6]["height"] == 283219
    assert result[7]["tx_hash"] == "d09ab213b5a7d5b8fc03d927c74d9bf2a97cd8f25dc0c20e12bed30c320ef008"
    assert result[7]["height"] == 283956
    assert result[8]["tx_hash"] == "4bc5ef4d9a44f141c67d71e57f57ae7d3f7f107ea0705a1fbe44304ac3aa5bfb"
    assert result[8]["height"] == 284286
    assert result[9]["tx_hash"] == "7045e8d5f9c11a4dbb6bf30d880cb098e146e6b3b9234950cf779bc9ec0ad26d"
    assert result[9]["height"] == 284341
    assert result[10]["tx_hash"] == "f503367c0fb6f22ca1535e7a297be6e15cc477d11dd3cfae533afa631044de1c"
    assert result[10]["height"] == 284997
    assert result[11]["tx_hash"] == "ab8822e3833bc3d0cb6537b577e5557ad09a63314a08c36acb72be012df2a7ba"
    assert result[11]["height"] == 285194
    assert result[12]["tx_hash"] == "34bb6f73c7eb9a51243584696ad6a80fbb4bbab199cb3274f38dadb350e581e5"
    assert result[12]["height"] == 286014
    assert result[13]["tx_hash"] == "0e92891d5dd7ca80cc53ec28c800fc0a35de7e0a0f53f68aa4d260097c26aca2"
    assert result[13]["height"] == 286321
    assert result[14]["tx_hash"] == "76b37d4055433cc037c5571dbe9468a550e85cd9ff9e4659dde099873bf3e254"
    assert result[14]["height"] == 287084
    assert result[15]["tx_hash"] == "b1ee005d28d36da0ae7a88b06ca5e08c8e858abfa51984c102b1f6f9fbaa4efa"
    assert result[15]["height"] == 287223


def test_blockchain_scripthash_get_history_3():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.scripthash.get_history",
        ["5f6046700aa7b611ecc5c22943d728b5cae1bf80a2aa98a4bf0b794e2c11762b"]
    )

    assert len(result) == 22
    assert result[0]["tx_hash"] == "2e9cbf9c53efff6d36e7ceb556d5e8cea70678dd9c7536cb3567a71e615996cd"
    assert result[0]["height"] == 399725
    assert result[1]["tx_hash"] == "fa80b433a309dc99f469ce8edc5971be5ab958ad9563ca05f74f69e7b6e898d2"
    assert result[1]["height"] == 399742
    assert result[2]["tx_hash"] == "dcde5af9a545a9eaf0fad5dc38aa83f827a05f7d9c96e8468e1c9c0794259ea4"
    assert result[2]["height"] == 399765
    assert result[3]["tx_hash"] == "5231b4e8886fe07534481f03288ad48ee0d17ee76d018e669d322cf33d04ff9c"
    assert result[3]["height"] == 399774
    assert result[4]["tx_hash"] == "73b332aba698f4f7918ccba413738be37b3d6f56043fb77bdd9a2a66bd765a0b"
    assert result[4]["height"] == 399777
    assert result[5]["tx_hash"] == "cc4380935316ae6e1af934fa018070dcee96225404cb125f7fe5ff7aad8cde11"
    assert result[5]["height"] == 399790
    assert result[6]["tx_hash"] == "c15a1b1e19f8f03c7a97ccb6cdf70c96529ec0cfaae25936bd521105e3d9faa6"
    assert result[6]["height"] == 406666
    assert result[7]["tx_hash"] == "45bf402a3179966db33ccd286c617ce1745294ad027d88283e7911d8ef17ac74"
    assert result[7]["height"] == 412380
    assert result[8]["tx_hash"] == "57b0f5c95c8efa9b5f385f2e834d36e925bb6f3637ff05ad70360077e560ef5d"
    assert result[8]["height"] == 413737
    assert result[9]["tx_hash"] == "38799b7e73e995978397dd33dc366378c2195844b9ca96cc2dedc5e3423f7591"
    assert result[9]["height"] == 417546
    assert result[10]["tx_hash"] == "d5d8a6532785653040e64952bc80a88d60bd1199bf8ec44a9d9417b37f03919a"
    assert result[10]["height"] == 419175
    assert result[11]["tx_hash"] == "cea683cbea99efeee30d56a789a3b5b92f4c821c6cb85f032aed342b43dcfff1"
    assert result[11]["height"] == 419177
    assert result[12]["tx_hash"] == "f165c30e31d7a441cf3793f4363a6ebed3def6f96932b9492beeccdb1deca40e"
    assert result[12]["height"] == 422128
    assert result[13]["tx_hash"] == "d83d4c03cb55a7888c2e5f49b34e3fa7085e210bb73e9386069dd6d2d07758c9"
    assert result[13]["height"] == 422808
    assert result[14]["tx_hash"] == "392ca398c77227fcb3b467463bee941f97b387709981b0c0ad1a5d9b3e910235"
    assert result[14]["height"] == 431801
    assert result[15]["tx_hash"] == "45901da6d790e590c3bc3d6d0f9a825585ee239d79250725485c20218229523d"
    assert result[15]["height"] == 431801
    assert result[16]["tx_hash"] == "45c5ea04cc6e2e932bf96ed595b9a809f7256558dbb18689080bda7207cb8046"
    assert result[16]["height"] == 436864
    assert result[17]["tx_hash"] == "1ff6d881103f17e5ac2f8dfde191d0fbc6b439401ed45572514eda108cf7b42f"
    assert result[17]["height"] == 441946
    assert result[18]["tx_hash"] == "b8577392faf642639e9e6755aff7d9b161252598b5a91f8923585d76795e5f0a"
    assert result[18]["height"] == 451449
    assert result[19]["tx_hash"] == "5ac941ba96bb66e3d78c65e73eb63df69a30f5f9900a09cd65a96981e14b9235"
    assert result[19]["height"] == 459707
    assert result[20]["tx_hash"] == "8dc9cebd2ce43e0ed1e3c3f9ad8cac0da72e860b476aa905bcad18af14d56910"
    assert result[20]["height"] == 464804
    assert result[21]["tx_hash"] == "8e7267d98c2838cffefb71579e09e0db6152fd4cde4bb4247601d3c01f7c08e4"
    assert result[21]["height"] == 467325


def test_blockchain_transaction_get_merkle():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.transaction.get_merkle",
        ["f8062887fd60638c71cdb7c06f1576477ba619d763b550708985de86be38eae7", 89988]
    )

    assert result["block_height"] == 89988
    assert result["pos"] == 2
    assert result["merkle"] == [
        "f8062887fd60638c71cdb7c06f1576477ba619d763b550708985de86be38eae7",
        "e0ea90c3440c91a8be2b51edaee73ace8edbdfc2d870e3ea32f878e7b61efaf0"
    ]


def test_blockchain_transaction_id_from_pos():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.transaction.id_from_pos",
        [86000, 1, False]
    )

    assert result == "51f33b7c4b9086fb693445054f358a40091eed87ba0bea70ba5f2deb7090fdad"


def test_blockchain_transaction_id_from_pos_merkle_1():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.transaction.id_from_pos",
        [86000, 1, True]
    )

    assert result["tx_hash"] == "51f33b7c4b9086fb693445054f358a40091eed87ba0bea70ba5f2deb7090fdad"
    assert result["merkle"] == [
        "f9caf63ec3df8609e7b9fbd78834c9f3bc0e92ce2c7e287ca8a806f8d02bdee2"
    ]


def test_blockchain_transaction_id_from_pos_merkle_2():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.transaction.id_from_pos",
        [89988, 2, True]
    )

    assert result["tx_hash"] == "f8062887fd60638c71cdb7c06f1576477ba619d763b550708985de86be38eae7"
    assert result["merkle"] == [
        "f8062887fd60638c71cdb7c06f1576477ba619d763b550708985de86be38eae7",
        "e0ea90c3440c91a8be2b51edaee73ace8edbdfc2d870e3ea32f878e7b61efaf0"
    ]


def test_blockchain_transaction_get_verbose_false():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.transaction.get",
        ["cc4380935316ae6e1af934fa018070dcee96225404cb125f7fe5ff7aad8cde11"]
    )

    assert result == (
        "0100000002198ae77007104e581f8fe0a8e20103c4119ccc9d8587a5b69e2b0fe4a29004cd000000006b483045022100"
        "c14014dcc75e50c6c7f6e210dfafee0353842f0c0d7df77f38af9144d9fe6319022049ad7505ad855e9b9bc903563ef9"
        "091e9abd3a13607ef0c52ea68b07eb279413012103a0dca7098edf6f0770ba000da89d57b9af27bd245056044ad2dfbb"
        "efd150eb6afeffffff5391cccb8cc44380b8a7e6acc8b4921a82de365ccb9aa964f81c0550ed7df49d010000006a4730"
        "4402202a5eef5a53c30102d926b562e4ff978217ebd729345a2e0349c38ac724def34f02202a88fdf55c1ea3f5b59934"
        "6ab829efe14f9673d31436b7ece169d37299765459012102cd6e3f23c335f96ac164bbc18933bfc591a49fe625ca9420"
        "e0dd55e34f892408feffffff02c0513000000000001976a9140cebeb5e2bd095eb8065ba8434bfbb9731b2304a88ac00"
        "b62902000000001976a914638ee59e1c83a5d2b179ee7f94c257ca22667bf888ac44190600"
    )


def test_blockchain_transaction_get_verbose_true():
    assert_electrumd_running()

    result = jsonrpc_send_request(
        ELECTRUMD_HOST,
        ELECTRUMD_PORT,
        "blockchain.transaction.get",
        ["cc4380935316ae6e1af934fa018070dcee96225404cb125f7fe5ff7aad8cde11", True]
    )

    assert result["txid"] == "cc4380935316ae6e1af934fa018070dcee96225404cb125f7fe5ff7aad8cde11"
    assert result["hash"] == "cc4380935316ae6e1af934fa018070dcee96225404cb125f7fe5ff7aad8cde11"
    assert result["version"] == 1
    assert result["size"] == 373
    assert result["vsize"] == 373
    assert result["weight"] == 1492
    assert result["locktime"] == 399684
    assert result["vin"] == [
        {
            "txid": "cd0490a2e40f2b9eb6a587859dcc9c11c40301e2a8e08f1f584e100770e78a19",
            "vout": 0,
            "scriptSig": {
                "asm": "3045022100c14014dcc75e50c6c7f6e210dfafee0353842f0c0d7df77f38af9"
                        "144d9fe6319022049ad7505ad855e9b9bc903563ef9091e9abd3a13607ef0c5"
                        "2ea68b07eb279413[ALL] 03a0dca7098edf6f0770ba000da89d57b9af27bd2"
                        "45056044ad2dfbbefd150eb6a",
                "hex": "483045022100c14014dcc75e50c6c7f6e210dfafee0353842f0c0d7df77f38a"
                        "f9144d9fe6319022049ad7505ad855e9b9bc903563ef9091e9abd3a13607ef0"
                        "c52ea68b07eb279413012103a0dca7098edf6f0770ba000da89d57b9af27bd24"
                        "5056044ad2dfbbefd150eb6a"
            },
            "sequence": 4294967294
        },
        {
            "txid": "9df47ded50051cf864a99acb5c36de821a92b4c8ace6a7b88043c48ccbcc9153",
            "vout": 1,
            "scriptSig": {
                "asm": "304402202a5eef5a53c30102d926b562e4ff978217ebd729345a2e0349c38ac7"
                        "24def34f02202a88fdf55c1ea3f5b599346ab829efe14f9673d31436b7ece169"
                        "d37299765459[ALL] 02cd6e3f23c335f96ac164bbc18933bfc591a49fe625ca"
                        "9420e0dd55e34f892408",
                "hex": "47304402202a5eef5a53c30102d926b562e4ff978217ebd729345a2e0349c38a"
                        "c724def34f02202a88fdf55c1ea3f5b599346ab829efe14f9673d31436b7ece1"
                        "69d37299765459012102cd6e3f23c335f96ac164bbc18933bfc591a49fe625ca"
                        "9420e0dd55e34f892408"
            },
            "sequence": 4294967294
        }
    ]
    assert result["vout"] == [
            {
                "value": 0.031667,
                "n": 0,
                "scriptPubKey": {
                    "asm": "OP_DUP OP_HASH160 0cebeb5e2bd095eb8065ba8434bfbb9731b2304a OP_EQUALVERIFY OP_CHECKSIG",
                    "desc": "addr(12BKjGe5Xj5bBXVVLy9ZgtWswewK87nC2G)#f9uc2u3x",
                    "hex": "76a9140cebeb5e2bd095eb8065ba8434bfbb9731b2304a88ac",
                    "address": "12BKjGe5Xj5bBXVVLy9ZgtWswewK87nC2G",
                    "type": "pubkeyhash"
                }
            },
            {
                "value": 0.36288000,
                "n": 1,
                "scriptPubKey": {
                    "asm": "OP_DUP OP_HASH160 638ee59e1c83a5d2b179ee7f94c257ca22667bf8 OP_EQUALVERIFY OP_CHECKSIG",
                    "desc": "addr(1A5R5eQYfVdhrXwafFfKgmTL4dZ8vpxDAK)#crnpjfgh",
                    "hex": "76a914638ee59e1c83a5d2b179ee7f94c257ca22667bf888ac",
                    "address": "1A5R5eQYfVdhrXwafFfKgmTL4dZ8vpxDAK",
                    "type": "pubkeyhash"
                }
            }
        ]
    assert result["hex"] == (
        "0100000002198ae77007104e581f8fe0a8e20103c4119ccc9d8587a5b69e2b0fe4a290"
        "04cd000000006b483045022100c14014dcc75e50c6c7f6e210dfafee0353842f0c0d7d"
        "f77f38af9144d9fe6319022049ad7505ad855e9b9bc903563ef9091e9abd3a13607ef0c"
        "52ea68b07eb279413012103a0dca7098edf6f0770ba000da89d57b9af27bd245056044a"
        "d2dfbbefd150eb6afeffffff5391cccb8cc44380b8a7e6acc8b4921a82de365ccb9aa96"
        "4f81c0550ed7df49d010000006a47304402202a5eef5a53c30102d926b562e4ff978217"
        "ebd729345a2e0349c38ac724def34f02202a88fdf55c1ea3f5b599346ab829efe14f967"
        "3d31436b7ece169d37299765459012102cd6e3f23c335f96ac164bbc18933bfc591a49f"
        "e625ca9420e0dd55e34f892408feffffff02c0513000000000001976a9140cebeb5e2bd"
        "095eb8065ba8434bfbb9731b2304a88ac00b62902000000001976a914638ee59e1c83a5"
        "d2b179ee7f94c257ca22667bf888ac44190600"
    )
    assert result["blockhash"] == "000000000000000002670430d6592079e4ab71154ed6462349d8615546d156ab"
    assert result["time"] == 1456282882
    assert result["blocktime"] == 1456282882
    assert result["confirmations"] > 497953


