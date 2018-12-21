# Rewards

Rewards, also sometimes called dividends, is a way to send tokenized assets or RVN to token holders.  This can be used to reward shareholders with profits (denominated in RVN), or reward membership holders, or reward those that contributed the most and earned special tokens.  

Rewards do not require a consensus protocol change, and the rpc calls exist to be able do rewards already.

These capabilities just make it native and easy-to-use from the client.

## Reward calculation

First, the QTY of TARGET_TOKEN is calculated.  This is the total issuance, minus the qty held by the exception addresses.

Next, the reward calculation takes the qty to send.  This must be specified, and will usually be RVN.

PER_TOKEN_AMOUNT_IN_SATOSHIS = [QTY TO SEND_IN_SATOSHIS] / [QTY OF TARGET_TOKEN]

For RVN this must send an equal number of satoshis to every TARGET_TOKEN.  Remainder satoshis should be sent to the miners.

For a token, this must send an equal number of the token to every TARGET_TOKEN.  The calculation will need to factor in the units.  For example, if you attempted to send 7 non-divisible (units=0) of SEND_TOKEN to every holder of TARGET_TOKEN, but there were 8 or more TARGET_TOKEN holders, then the 'reward' call would fail because it is impossible to reward the TARGET_TOKEN holders equally.

Example: 10 RVN to 3 TRONCO holders.  10 * 100,000,000 sats = 1,000,000,000 RVN sats.
PER_TOKEN_AMOUNT_IN_SATOSHIS = 1,000,000,000 / 3 = 333333333.33333333 (repeating) per TRONCO holder.  The remainder of .3333 (repeating) satoshis per holder will not be sent as an output, and therefore will be given to the miners.  This is 1 sat once multiplied by the 3 TRONCO holders.  Each TRONCO holder will receive exactly 333333333 RVN sats.

One special case - Paying TRONCO to TRONCO.  This special case would require an exception address, and the source of the TRONCO would need to come from one or more of the exception addresses.
reward 400000 TRONCO TRONCO ['exception address']

### Rewards Components
#### Protocol

No protocol change needed.

#### GUI - Desktop

Select a token or RVN to send.
Set the QTY to send.
Select the target token
* If you are sending another token, you must have the owner token for the TARGET_TOKEN.
* If you are sending RVN, you do not need the owner token for the TARGET_TOKEN

GUI will show the exact amount being sent to each TARGET_TOKEN.  It will also calculate and show the remaining which must be returned as change.  If RVN is being sent, it will show the remainder that is being sent to the miners.

#### GUI - Mobile

Mobile will not initially have the rewards feature.

#### RPC

These rpc calls are added in support of rewards:

reward QTY [RVN|TOKEN] TARGET_TOKEN [exception address list]
