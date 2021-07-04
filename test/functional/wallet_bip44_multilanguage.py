#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test the Wallet BIP44 12 words implementation and supporting RPC"""

import os
from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_does_not_contain, assert_contains, assert_raises_rpc_error
from test_framework.wallet_util import bip39_spanish
from test_framework.wallet_util import bip39_english
from test_framework.wallet_util import bip39_french
from test_framework.wallet_util import bip39_japanese
from test_framework.wallet_util import bip39_chinese_simplified
from test_framework.wallet_util import bip39_chinese_traditional
from test_framework.wallet_util import bip39_korean
from test_framework.wallet_util import bip39_italian

MNEMONIC_0 = 'puma árbol trago pálido oruga fibra candil jurar humano archivo envase capaz' #spanish
MNEMONIC_PASS_0 = 'test0'
MNEMONIC_1 = 'sol foto remo casa oreja cita grasa desayuno filial profesor pan salero' #spanish
MNEMONIC_2 = 'insolite hésiter gazelle ruche anxieux fluvial orange carton linéaire lueur ovation ennuyeux' #french
MNEMONIC_PASS_2 = 'test2'
MNEMONIC_3 = 'ぞんび せんぞ ずっしり ふこう いほう しゃちょう なにもの かくとく たんおん だんわ にうけ こゆう' #japanese
MNEMONIC_PASS_3 = 'test3'
MNEMONIC_4 = '这 飞 炭 亦 偿 锦 昏 燕 炒 徐 军 讯' #chinese simplified
MNEMONIC_PASS_4 = 'test4'
MNEMONIC_5 = '這 飛 炭 亦 償 錦 昏 燕 炒 徐 軍 訊' #chinese traditional
MNEMONIC_PASS_5 = 'test5'
MNEMONIC_6 = '가슴 법적 잔디 약혼녀 출발 편지 해결 하천 체중 영국 교문 절망' #korean
MNEMONIC_PASS_6 = 'test6'
MNEMONIC_7 = 'monviso marcire lavagna snodo appunto inodore radunato ceto olandese orecchino ravveduto fontana' #italian
MNEMONIC_PASS_7 = 'test7'

class Bip44Test(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 8
        self.extra_args = [['-bip44=1', '-mnemonic=' + MNEMONIC_0, '-mnemonicpassphrase=' + MNEMONIC_PASS_0], # BIP44 wallet with user-generated 12-words and passphrase
                           ['-bip44=1', '-mnemonic=' + MNEMONIC_1], # BIP44 wallet with user-generated 12-words, but no passphrase
                           ['-bip44=1', '-mnemonic=' + MNEMONIC_2, '-mnemonicpassphrase=' + MNEMONIC_PASS_2], # BIP44 wallet with user-generated 12-words and passphrase
                           ['-bip44=1', '-mnemonic=' + MNEMONIC_3, '-mnemonicpassphrase=' + MNEMONIC_PASS_3], # BIP44 wallet with user-generated 12-words and passphrase
                           ['-bip44=1', '-mnemonic=' + MNEMONIC_4, '-mnemonicpassphrase=' + MNEMONIC_PASS_4], # BIP44 wallet with user-generated 12-words and passphrase
                           ['-bip44=1', '-mnemonic=' + MNEMONIC_5, '-mnemonicpassphrase=' + MNEMONIC_PASS_5], # BIP44 wallet with user-generated 12-words and passphrase
                           ['-bip44=1', '-mnemonic=' + MNEMONIC_6, '-mnemonicpassphrase=' + MNEMONIC_PASS_6], # BIP44 wallet with user-generated 12-words and passphrase
                           ['-bip44=1', '-mnemonic=' + MNEMONIC_7, '-mnemonicpassphrase=' + MNEMONIC_PASS_7] # BIP44 wallet with user-generated 12-words and passphrase
                        ]

    def run_test(self):
        nodes = self.nodes

        # BIP39 list should contain 2048 words
        assert_equal(len(bip39_spanish), 2048)
        assert_equal(len(bip39_french), 2048)
        assert_equal(len(bip39_japanese), 2048)
        assert_equal(len(bip39_chinese_simplified), 2048)
        assert_equal(len(bip39_chinese_traditional), 2048)
        assert_equal(len(bip39_korean), 2048)
        assert_equal(len(bip39_italian), 2048)

        # Node 0 has 12 words and a passphrase
        self.log.info("Testing BIP-44 Word-Lists and passphrases")
        assert_equal(len(nodes[0].getmywords()['word_list'].split(' ')), 12) # Contains 12 words
        assert_equal(nodes[0].getmywords()['word_list'], MNEMONIC_0) # Word list matches
        assert_equal(nodes[0].getmywords()['passphrase'], MNEMONIC_PASS_0) # Passphrase matches

        # Node 1 has 12 words but no passphrase
        assert_equal(len(nodes[1].getmywords()['word_list'].split(' ')), 12) # Contains 12 words
        assert_equal(nodes[1].getmywords()['word_list'], MNEMONIC_1) # Word list matches
        assert_does_not_contain(str(nodes[1].getmywords()), 'passphrase') # Passphrase does not exist

        # Node 2 has 12 words and a passphrase
        assert_equal(len(nodes[2].getmywords()['word_list'].split(' ')), 12) # Contains 12 words
        assert_equal(nodes[2].getmywords()['word_list'], MNEMONIC_2) # Word list matches
        assert_equal(nodes[2].getmywords()['passphrase'], MNEMONIC_PASS_2) # Passphrase matches

        # Node 3 has 12 words and a passphrase
        assert_equal(len(nodes[3].getmywords()['word_list'].split(' ')), 12) # Contains 12 words
        assert_equal(nodes[3].getmywords()['word_list'], MNEMONIC_3) # Word list matches
        assert_equal(nodes[3].getmywords()['passphrase'], MNEMONIC_PASS_3) # Passphrase matches

        # Node 4 has 12 words and a passphrase
        assert_equal(len(nodes[4].getmywords()['word_list'].split(' ')), 12) # Contains 12 words
        assert_equal(nodes[4].getmywords()['word_list'], MNEMONIC_4) # Word list matches
        assert_equal(nodes[4].getmywords()['passphrase'], MNEMONIC_PASS_4) # Passphrase matches

        # Node 5 has 12 words and a passphrase
        assert_equal(len(nodes[5].getmywords()['word_list'].split(' ')), 12) # Contains 12 words
        assert_equal(nodes[5].getmywords()['word_list'], MNEMONIC_5) # Word list matches
        assert_equal(nodes[5].getmywords()['passphrase'], MNEMONIC_PASS_5) # Passphrase matches

        # Node 6 has 12 words and a passphrase
        assert_equal(len(nodes[6].getmywords()['word_list'].split(' ')), 12) # Contains 12 words
        assert_equal(nodes[6].getmywords()['word_list'], MNEMONIC_6) # Word list matches
        assert_equal(nodes[6].getmywords()['passphrase'], MNEMONIC_PASS_6) # Passphrase matches

        # Node 7 has 12 words and a passphrase
        assert_equal(len(nodes[7].getmywords()['word_list'].split(' ')), 12) # Contains 12 words
        assert_equal(nodes[7].getmywords()['word_list'], MNEMONIC_7) # Word list matches
        assert_equal(nodes[7].getmywords()['passphrase'], MNEMONIC_PASS_7) # Passphrase matches

        # Try to add a passphrase to an existing bip44 wallet (should not add passphrase)
        self.stop_node(3)
        self.start_node(3, extra_args=['-mnemonicpassphrase=test3'])
        assert_does_not_contain(str(nodes[3].getmywords()), 'passphrase') # Passphrase does not exist

        # Cannot change an already created bip44 wallet to a non-bip44 wallet
        word_list_3 = nodes[3].getmywords()['word_list']
        self.stop_node(3)
        self.start_node(3, extra_args=['-bip44=0'])
        assert_equal(nodes[3].getmywords()['word_list'], word_list_3) # Word list matches

        # All 4 bip44 enabled wallets word-lists are in the bip39 word-list
        self.log.info("Testing that BIP-44 wallets words are valid")
        word_list_0 = MNEMONIC_0.split(' ')
        word_list_1 = MNEMONIC_1.split(' ')
        word_list_2 = MNEMONIC_2.split(' ')
        word_list_3 = MNEMONIC_3.split(' ')
        word_list_4 = MNEMONIC_4.split(' ')
        word_list_5 = MNEMONIC_5.split(' ')
        word_list_6 = MNEMONIC_6.split(' ')
        word_list_7 = MNEMONIC_7.split(' ')
        for i in range(0, 12):
            assert_contains(word_list_0[i], bip39_spanish)
            assert_contains(word_list_1[i], bip39_spanish)
            assert_contains(word_list_2[i], bip39_french)
            assert_contains(word_list_3[i], bip39_japanese)
            assert_contains(word_list_4[i], bip39_chinese_simplified)
            assert_contains(word_list_5[i], bip39_chinese_traditional)
            assert_contains(word_list_6[i], bip39_korean)
            assert_contains(word_list_7[i], bip39_italian)

        # None of the words should be text-readable in the log files
        self.log.info("Testing that BIP-44 words aren't text readable")
        mnemonic_1 = nodes[1].getmywords()['word_list']
        self.stop_nodes()
        with open(os.path.join(self.options.tmpdir+"/node0/regtest/", "debug.log"), 'r', encoding='utf8') as f:
            assert_does_not_contain(MNEMONIC_0, f.read())
        with open(os.path.join(self.options.tmpdir+"/node1/regtest/", "debug.log"), 'r', encoding='utf8') as f:
            assert_does_not_contain(MNEMONIC_1, f.read())
        with open(os.path.join(self.options.tmpdir+"/node2/regtest/", "debug.log"), 'r', encoding='utf8') as f:
            assert_does_not_contain(MNEMONIC_2, f.read())
        with open(os.path.join(self.options.tmpdir+"/node3/regtest/", "debug.log"), 'r', encoding='utf8') as f:
            assert_does_not_contain(MNEMONIC_3, f.read())

        # But words are readable in a non-encrypted wallet
      
        with open(os.path.join(self.options.tmpdir+"/node1/regtest/", "wallet.dat"), 'rb') as f:
            assert_contains(mnemonic_1, str(f.read()))

        # After encryption the words are no longer readable
        self.log.info("Testing that BIP-44 wallet encryption")
        self.start_nodes()
        nodes[0].node_encrypt_wallet("password0")
        nodes[1].node_encrypt_wallet("password1")
        nodes[2].node_encrypt_wallet("password2")
        nodes[3].node_encrypt_wallet("password3")
        nodes[4].node_encrypt_wallet("password4")
        nodes[5].node_encrypt_wallet("password5")
        nodes[6].node_encrypt_wallet("password6")
        nodes[7].node_encrypt_wallet("password7")
        self.stop_nodes()
        # But words are not readable in an encrypted wallet
        with open(os.path.join(self.options.tmpdir+"/node0/regtest/", "wallet.dat"), 'rb') as f:
            assert_does_not_contain(MNEMONIC_0, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node1/regtest/", "wallet.dat"), 'rb') as f:
            assert_does_not_contain(MNEMONIC_1, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node2/regtest/", "wallet.dat"), 'rb') as f:
            assert_does_not_contain(MNEMONIC_2, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node3/regtest/", "wallet.dat"), 'rb') as f:
            assert_does_not_contain(MNEMONIC_3, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node4/regtest/", "wallet.dat"), 'rb') as f:
            assert_does_not_contain(MNEMONIC_4, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node5/regtest/", "wallet.dat"), 'rb') as f:
            assert_does_not_contain(MNEMONIC_5, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node6/regtest/", "wallet.dat"), 'rb') as f:
            assert_does_not_contain(MNEMONIC_6, str(f.read()))
        with open(os.path.join(self.options.tmpdir+"/node7/regtest/", "wallet.dat"), 'rb') as f:
            assert_does_not_contain(MNEMONIC_7, str(f.read()))

        # But the words are still available using getmywords after entering the passphrase
        self.start_nodes()
        assert_raises_rpc_error(-13, "Please enter the wallet passphrase with walletpassphrase first.", nodes[0].getmywords)
        nodes[0].walletpassphrase("password0", 48)
        assert_equal(nodes[0].getmywords()['word_list'], MNEMONIC_0) # Word list matches

        # Words can also be retrieved from the dumpwallet command
        nodes[0].dumpwallet(os.path.join(self.options.tmpdir+"/node0/regtest/", "dump_wallet_0.txt"))
        with open(os.path.join(self.options.tmpdir+"/node0/regtest/", "dump_wallet_0.txt"), 'r') as f:
            assert_contains(MNEMONIC_0, str(f.read()))

if __name__ == '__main__':
    Bip44Test().main()