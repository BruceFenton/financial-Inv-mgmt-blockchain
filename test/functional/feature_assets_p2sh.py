#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017-2021 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Testing asset use cases"""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, assert_is_hash_string, assert_does_not_contain_key, assert_raises_rpc_error, JSONRPCException, Decimal

import string


class AssetTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [['-assetindex'], ['-assetindex'], ['-assetindex']]

    def activate_p2sh_assets(self):
        self.log.info("Generating RVN for node[0] and activating assets...")
        n0 = self.nodes[0]

        n0.generate(1)
        self.sync_all()
        n0.generate(431)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['p2sh_assets']['status'])

    def p2sh_issue_asset_test(self):
        self.log.info("Running p2sh_issue_asset_test")
        n0 = self.nodes[0]

        asset_name = "P2SH_ISSUED"

        # Create 1 of 1 multisig P2SH address
        multisig_address = n0.createmultisig(1, [n0.getnewaddress()])['address']

        # Issue Asset to multisig address
        self.log.info("Calling issue() to P2SH address")
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        issue_txid = n0.issue(asset_name=asset_name, qty=1000, to_address=multisig_address, change_address="",
                              units=8, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)

        self.log.info("Waiting for one confirmations after issue...")
        n0.generate(1)
        self.sync_all()

        # Check asset data was created correctly
        self.log.info("Checkout getassetdata()...")
        assetdata = n0.getassetdata(asset_name)
        assert_equal(assetdata["name"], asset_name)
        assert_equal(assetdata["amount"], 1000)
        assert_equal(assetdata["units"], 8)
        assert_equal(assetdata["reissuable"], 1)
        assert_equal(assetdata["has_ipfs"], 1)
        assert_equal(assetdata["ipfs_hash"], ipfs_hash)

    def p2sh_1of2_single_node_asset_transfer_test(self):
        self.log.info("Running p2sh_1of2_single_node_asset_transfer_test")
        n0, n1 = self.nodes[0], self.nodes[1]

        asset_name = "P2SH_ASSET_SINGLE"

        # Create new address
        # Get private key for address
        # Create multisig address
        # Gather multisig address and redeem script
        first_address = n0.getnewaddress()
        second_address = n0.getnewaddress()

        first_address_pub = n0.validateaddress(first_address)['pubkey']
        second_address_pub = n0.validateaddress(second_address)['pubkey']

        first_address_priv = n0.dumpprivkey(first_address)
        second_address_priv = n0.dumpprivkey(second_address)

        multisig_data = n0.createmultisig(1, [first_address_pub, second_address_pub])
        multisig_address = multisig_data['address']
        multisig_redeemscript = multisig_data['redeemScript']

        # Issue Asset to multisig address
        self.log.info("Calling issue() to P2SH address")
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        issue_txid = n0.issue(asset_name=asset_name, qty=1000, to_address=multisig_address, change_address="",
                              units=8, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)

        self.log.info("Waiting for one confirmations after P2SH issue...")
        n0.generate(1)
        self.sync_all()

        tx_data = n0.decoderawtransaction(n0.gettransaction(issue_txid[0])['hex'])
        tx_index = 3

        # Get scriptpubkey for issued asset location
        issued_scriptPubKey = tx_data['vout'][tx_index]['scriptPubKey']['hex']

        # Building createrawtransaction data
        rvn_destination_address = n0.getnewaddress()

        self.log.info("Get RVN for tx fee()...")
        unspent_rvn_inputs = n0.listunspent(100)[0]

        self.log.info("Get private key for unspent input")
        unspent_rvn_private_key = n0.dumpprivkey(unspent_rvn_inputs['address'])

        self.log.info("Building rvn tx input...")
        rvn_tx_input = {
            'txid' : unspent_rvn_inputs['txid'],
            'vout' : unspent_rvn_inputs['vout']
        }

        self.log.info("Building asset tx input...")
        asset_tx_input = {
            'txid' : issue_txid[0],
            'vout' : tx_index
        }

        self.log.info("Building destination tx input...")
        destination_tx_data = {
            rvn_destination_address : unspent_rvn_inputs['amount'] - 1,
            first_address : {
                'transfer' : {
                    asset_name : 1000
                }
            }
        }

        # Create data for signrawtransaction()
        self.log.info("Calling createrawtransaction()...")
        create_raw_hex = n0.createrawtransaction([rvn_tx_input, asset_tx_input], destination_tx_data)

        prev_tx_data = [
            {
                'txid' : rvn_tx_input['txid'],
                'vout' : rvn_tx_input['vout'],
                'scriptPubKey' : unspent_rvn_inputs['scriptPubKey'],
                'redeemScript' : '',
                'amount' : unspent_rvn_inputs['amount']
            },
            {
                'txid' : asset_tx_input['txid'],
                'vout' : asset_tx_input['vout'],
                'scriptPubKey' : issued_scriptPubKey,
                'redeemScript' : multisig_redeemscript,
                'amount' : 0
            }
        ]

        self.log.info("Calling signrawtransaction on first node with one private key...")
        private_keys = [first_address_priv, unspent_rvn_private_key]
        signed_data = n0.signrawtransaction(create_raw_hex, prev_tx_data, private_keys)
        assert_equal(signed_data['complete'], True)

        self.log.info("Calling signrawtransaction on first node with second private key...")
        private_keys = [second_address_priv, unspent_rvn_private_key]
        signed_data = n0.signrawtransaction(create_raw_hex, prev_tx_data, private_keys)
        assert_equal(signed_data['complete'], True)

        self.log.info("Calling sendrawtransaction...")
        txid = n0.sendrawtransaction(signed_data['hex'])

        assert_equal(len(txid), 64)


    def p2sh_2of3_multi_nodes_asset_transfer_test(self):
        self.log.info("Running p2sh_2of3_multi_nodes_asset_transfer_test")
        n0, n1 = self.nodes[0], self.nodes[1]

        asset_name = "P2SH_ASSET_MULTIPLE"

        # Create new address
        # Get private key for address
        # Create multisig address
        # Gather multisig address and redeem script
        first_address = n0.getnewaddress()
        second_address = n1.getnewaddress()
        third_address = n1.getnewaddress()

        first_address_pub = n0.validateaddress(first_address)['pubkey']
        second_address_pub = n1.validateaddress(second_address)['pubkey']
        third_address_pub = n1.validateaddress(third_address)['pubkey']

        first_address_priv = n0.dumpprivkey(first_address)
        second_address_priv = n1.dumpprivkey(second_address)
        third_addrress_priv = n1.dumpprivkey(third_address)

        multisig_data = n0.createmultisig(2, [first_address_pub, second_address_pub, third_address_pub])
        n1.createmultisig(2, [first_address_pub, second_address_pub, third_address_pub])
        multisig_address = multisig_data['address']
        multisig_redeemscript = multisig_data['redeemScript']

        # txid = n0.sendtoaddress(multisig_address, 5000)

        # Issue Asset to multisig address
        self.log.info("Calling issue() to P2SH address")
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        issue_txid = n0.issue(asset_name=asset_name, qty=1000, to_address=multisig_address, change_address="",
                              units=8, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)


        self.log.info("Waiting for one confirmations after P2SH issue...")
        n0.generate(1)
        self.sync_all()

        tx_data = n0.decoderawtransaction(n0.gettransaction(issue_txid[0])['hex'])
        tx_outs = tx_data['vout']
        tx_index = 3

        # Get scriptpubkey for issued asset location
        issued_scriptPubKey = tx_data['vout'][tx_index]['scriptPubKey']['hex']

        # Building createrawtransaction data
        rvn_destination_address = n0.getnewaddress()

        self.log.info("Get RVN for tx fee()...")
        unspent_rvn_inputs = n0.listunspent(100)[0]

        self.log.info("Get private key for unspent input")
        unspent_rvn_private_key = n0.dumpprivkey(unspent_rvn_inputs['address'])

        self.log.info("Building rvn tx input...")
        rvn_tx_input = {
            'txid' : unspent_rvn_inputs['txid'],
            'vout' : unspent_rvn_inputs['vout']
        }

        self.log.info("Building asset tx input...")
        asset_tx_input = {
            'txid' : issue_txid[0],
            'vout' : tx_index
        }

        self.log.info("Building destination tx input...")
        destination_tx_data = {
            rvn_destination_address : unspent_rvn_inputs['amount'] - 1,
            first_address : {
                'transfer' : {
                    asset_name : 1000
                }
            }
        }

        # Create data for signrawtransaction()
        self.log.info("Calling createrawtransaction()...")
        create_raw_hex = n0.createrawtransaction([rvn_tx_input, asset_tx_input], destination_tx_data)

        prev_tx_data = [
            {
                'txid' : rvn_tx_input['txid'],
                'vout' : rvn_tx_input['vout'],
                'scriptPubKey' : unspent_rvn_inputs['scriptPubKey'],
                'redeemScript' : '',
                'amount' : unspent_rvn_inputs['amount']
            },
            {
                'txid' : asset_tx_input['txid'],
                'vout' : asset_tx_input['vout'],
                'scriptPubKey' : issued_scriptPubKey,
                'redeemScript' : multisig_redeemscript,
                'amount' : 0
            }
        ]

        self.log.info("Calling signrawtransaction on first node...")
        private_keys = [first_address_priv, unspent_rvn_private_key]
        signed_data = n0.signrawtransaction(create_raw_hex, prev_tx_data, private_keys)
        assert_equal(signed_data['complete'], False)
        partial_signed_hex = signed_data['hex']


        self.log.info("Calling signrawtransaction on second node...")
        private_keys = [third_addrress_priv]
        signed_data = n1.signrawtransaction(partial_signed_hex, prev_tx_data, private_keys)
        assert_equal(signed_data['complete'], True)

        self.log.info("Calling sendrawtransaction...")
        txid = n0.sendrawtransaction(signed_data['hex'])

        assert_equal(len(txid), 64)

    def p2sh_2of3_multi_nodes_rvn_send_test(self):
        self.log.info("Running p2sh_2of3_multi_nodes_rvn_send_test")
        n0, n1 = self.nodes[0], self.nodes[1]

        # Create new address
        # Get private key for address
        # Create multisig address
        # Gather multisig address and redeem script
        first_address = n0.getnewaddress()
        second_address = n1.getnewaddress()
        third_address = n1.getnewaddress()

        first_address_pub = n0.validateaddress(first_address)['pubkey']
        second_address_pub = n1.validateaddress(second_address)['pubkey']
        third_address_pub = n1.validateaddress(third_address)['pubkey']

        first_address_priv = n0.dumpprivkey(first_address)
        second_address_priv = n1.dumpprivkey(second_address)
        third_addrress_priv = n1.dumpprivkey(third_address)

        multisig_data = n0.createmultisig(2, [first_address_pub, second_address_pub, third_address_pub])
        n1.createmultisig(2, [first_address_pub, second_address_pub, third_address_pub])
        multisig_address = multisig_data['address']
        multisig_redeemscript = multisig_data['redeemScript']

        txid = n0.sendtoaddress(multisig_address, 5000)

        self.log.info("Waiting for one confirmations after P2SH issue...")
        n0.generate(1)
        self.sync_all()

        tx_data = n0.decoderawtransaction(n0.gettransaction(txid)['hex'])
        tx_outs = tx_data['vout']
        tx_index = -1

        for i in tx_outs:
            if (multisig_address in i['scriptPubKey']['addresses']):
                tx_index = i['n']
                break

        # Get scriptpubkey for issued asset location
        issued_scriptPubKey = tx_data['vout'][tx_index]['scriptPubKey']['hex']

        # Building createrawtransaction data
        rvn_destination_address = n0.getnewaddress()

        self.log.info("Building rvn tx input...")
        rvn_tx_input = {
            'txid' : txid,
            'vout' : tx_index
        }

        self.log.info("Building destination tx input...")
        destination_tx_data = {
            rvn_destination_address : 4999
        }

        self.log.info("Calling createrawtransaction()...")
        create_raw_hex = n0.createrawtransaction([rvn_tx_input], destination_tx_data)

        # Create data for signrawtransaction()
        self.log.info("Building prevTx Data...")
        prev_tx_data = [
            {
                'txid' : txid,
                'vout' : tx_index,
                'scriptPubKey' : issued_scriptPubKey,
                'redeemScript' : multisig_redeemscript,
                'amount' : 5000
            }
        ]

        self.log.info("Calling signrawtransaction on first node...")
        private_keys = [first_address_priv]
        signed_data = n0.signrawtransaction(create_raw_hex, prev_tx_data, private_keys)
        assert_equal(signed_data['complete'], False)
        partial_signed_hex = signed_data['hex']

        self.log.info("Calling signrawtransaction on second node...")
        private_keys = [third_addrress_priv]
        signed_data = n1.signrawtransaction(partial_signed_hex, prev_tx_data, private_keys)
        assert_equal(signed_data['complete'], True)

        self.log.info("Calling sendrawtransaction...")
        txid = n0.sendrawtransaction(signed_data['hex'])

        assert_equal(len(txid), 64)

    def p2sh_using_rpc_console_test(self):
        self.log.info("Running p2sh_using_rpc_console_test")
        n0, n1 = self.nodes[0], self.nodes[1]

        asset_name = 'P2SH_ASSET_RPC_CONSOLE'

        # Generate address
        first_address = n0.getnewaddress()

        # Create a 1-of-1 P2SH multisig address
        multisig_address = n0.addmultisigaddress(1, [first_address])

        # Issue asset to multisig address
        self.log.info("Calling issue() to P2SH address")
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        issue_txid = n0.issue(asset_name=asset_name, qty=100, to_address=multisig_address, change_address="",
                              units=8, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)

        self.log.info("Waiting for one confirmations after P2SH issue...")
        n0.generate(1)
        self.sync_all()

        # Check to see if asset is in wallet asset list
        my_assets = n0.listmyassets(asset_name, True)

        self.log.info("Checking if asset data is in our wallet...")
        assert_equal(asset_name in my_assets, True)
        assert_equal(asset_name + '!' in my_assets, True)
        assert_equal(my_assets[asset_name]['balance'], 100)
        assert_equal(my_assets[asset_name + '!']['balance'], 1)


        self.log.info("Calling transfer() to a P2PKH address...")
        transfer_txid = n0.transfer(asset_name, 50, first_address, "", 0, "", multisig_address)

        self.log.info("Waiting for one confirmations after P2SH transfer...")
        n0.generate(1)
        self.sync_all()

        self.log.info("Checking address database")
        balances_P2PKH = n0.listassetbalancesbyaddress(first_address)
        balances_P2SH = n0.listassetbalancesbyaddress(multisig_address)
        assert_equal(balances_P2PKH[asset_name], 50)
        assert_equal(balances_P2SH[asset_name], 50)
        assert_equal(balances_P2SH[asset_name + '!'], 1)

    def p2sh_1of2_single_node_signatue_mutated_attack_test(self):
        self.log.info("Running p2sh_1of2_single_node_signatue_mutated_attack_test")
        n0, n1 = self.nodes[0], self.nodes[1]

        asset_name = "P2SH_ASSET_MUTATED"

        # Create new address
        # Get private key for address
        # Create multisig address
        # Gather multisig address and redeem script
        first_address = n0.getnewaddress()
        second_address = n0.getnewaddress()

        first_address_pub = n0.validateaddress(first_address)['pubkey']
        second_address_pub = n0.validateaddress(second_address)['pubkey']

        first_address_priv = n0.dumpprivkey(first_address)
        second_address_priv = n0.dumpprivkey(second_address)

        multisig_data = n0.createmultisig(1, [first_address_pub, second_address_pub])
        multisig_address = multisig_data['address']
        multisig_redeemscript = multisig_data['redeemScript']

        # Issue Asset to multisig address
        self.log.info("Calling issue() to P2SH address")
        ipfs_hash = "QmcvyefkqQX3PpjpY5L8B2yMd47XrVwAipr6cxUt2zvYU8"
        issue_txid = n0.issue(asset_name=asset_name, qty=1000, to_address=multisig_address, change_address="",
                              units=8, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)

        self.log.info("Waiting for one confirmations after P2SH issue...")
        n0.generate(1)
        self.sync_all()

        tx_data = n0.decoderawtransaction(n0.gettransaction(issue_txid[0])['hex'])
        tx_index = 3

        # Get scriptpubkey for issued asset location
        issued_scriptPubKey = tx_data['vout'][tx_index]['scriptPubKey']['hex']

        # Building createrawtransaction data
        rvn_destination_address = n0.getnewaddress()

        self.log.info("Get RVN for tx fee()...")
        unspent_rvn_inputs = n0.listunspent(100)[0]

        self.log.info("Get private key for unspent input")
        unspent_rvn_private_key = n0.dumpprivkey(unspent_rvn_inputs['address'])

        self.log.info("Building rvn tx input...")
        rvn_tx_input = {
            'txid' : unspent_rvn_inputs['txid'],
            'vout' : unspent_rvn_inputs['vout']
        }

        self.log.info("Building asset tx input...")
        asset_tx_input = {
            'txid' : issue_txid[0],
            'vout' : tx_index
        }

        self.log.info("Building destination tx input...")
        destination_tx_data = {
            rvn_destination_address : unspent_rvn_inputs['amount'] - 1,
            first_address : {
                'transfer' : {
                    asset_name : 1000
                }
            }
        }

        # Create data for signrawtransaction()
        self.log.info("Calling createrawtransaction()...")
        create_raw_hex = n0.createrawtransaction([rvn_tx_input, asset_tx_input], destination_tx_data)

        prev_tx_data = [
            {
                'txid' : rvn_tx_input['txid'],
                'vout' : rvn_tx_input['vout'],
                'scriptPubKey' : unspent_rvn_inputs['scriptPubKey'],
                'redeemScript' : '',
                'amount' : unspent_rvn_inputs['amount']
            },
            {
                'txid' : asset_tx_input['txid'],
                'vout' : asset_tx_input['vout'],
                'scriptPubKey' : issued_scriptPubKey,
                'redeemScript' : multisig_redeemscript,
                'amount' : 0
            }
        ]

        self.log.info("Calling signrawtransaction on first node with one private key...")
        private_keys = [first_address_priv, unspent_rvn_private_key]
        signed_data = n0.signrawtransaction(create_raw_hex, prev_tx_data, private_keys)
        assert_equal(signed_data['complete'], True)

        # Mutate the signature belonging to the P2SH asset transfer
        signed_transaction_hex = signed_data['hex']
        asset_signature_hex = n0.decoderawtransaction(signed_transaction_hex)['vin'][1]['scriptSig']['hex']
        mutate_location = 25

        # Change the signature by changing a single hex character at index 25
        signature_list = list(asset_signature_hex)
        if signature_list[mutate_location] is '0':
            signature_list[mutate_location] = '1'
        else:
            signature_list[mutate_location] = '0'

        # Create a string from the list, and replace the signature in the signed transaction with the mutated one
        mutated_signature_hex = "".join(signature_list)
        mutated_hex = signed_transaction_hex.replace(asset_signature_hex,mutated_signature_hex)

        assert_raises_rpc_error(-26, "mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)",  n0.sendrawtransaction, mutated_hex)


    def run_test(self):
        self.activate_p2sh_assets()
        self.p2sh_issue_asset_test()
        self.p2sh_1of2_single_node_asset_transfer_test()
        self.p2sh_2of3_multi_nodes_rvn_send_test()
        self.p2sh_2of3_multi_nodes_asset_transfer_test()
        self.p2sh_using_rpc_console_test()
        self.p2sh_1of2_single_node_signatue_mutated_attack_test()


if __name__ == '__main__':
    AssetTest().main()
