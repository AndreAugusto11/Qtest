const { Chain } = require("qtest-js");
const { keccak256 } = require('js-sha3');
const { createStorageKey, uint256, addToKey, addressToUint256, stringToStorageValue } = require('./test-utils');

// EVM Storage Slot Constants (must match Solidity contract storage layout)
const STORAGE_REGISTER_PAIR_INDEX = 4;      // PairBridgeNFTRegister: pairs array
const STORAGE_REGISTER_REQUEST_INDEX = 5;   // PairBridgeNFTRegister: requests array
const STORAGE_BRIDGE_REQUEST_INDEX = 5;     // TokenBridgeNFT: requests array
const STORAGE_BRIDGE_REFUND_INDEX = 6;      // TokenBridgeNFT: refunds array

describe("Bridge EVM Storage Reading", () => {
    let chain;
    let evmAccount;
    let evmContract;

    beforeAll(async () => {
        chain = await Chain.setupChain(process.env.CHAIN_NAME || 'TLOS');

        // Create accounts
        evmAccount = await chain.system.createAccount("eosio.evm");

        // Deploy mock eosio.evm
        evmContract = await evmAccount.setContract({
            abi: "./build/mock.evm.abi",
            wasm: "./build/mock.evm.wasm"
        });
        console.log("✓ Mock eosio.evm deployed");

    }, 60000);

    afterAll(async () => {
        await chain.clear();
    }, 10000);

    describe(":: Read EVM Requests", () => {
        it("Should read request array length from EVM storage", async () => {
            const bridgeScope = 123; // Mock scope
            
            // requests array length
            console.log("Setting mock request array length in EVM storage...");
            const lengthKey = createStorageKey(STORAGE_BRIDGE_REQUEST_INDEX);
            console.log("Length storage key:", lengthKey);
            const lengthValue = uint256(2); // 2 requests
            console.log("Length value (uint256):", lengthValue);
            
            // Set mock storage
            await evmContract.action.setstate({
                scope: bridgeScope,
                key: lengthKey,
                value_low: lengthValue.value_low,
                value_high: lengthValue.value_high
            }, [{ actor: evmAccount.name, permission: "active" }]);
            console.log("✓ Mock request array length set in EVM storage");
            
            // Verify storage was set
            const stateTable = await chain.rpc.get_table_rows({
                json: true,
                code: evmAccount.name,
                scope: bridgeScope,
                table: "accountstate"
            });
            
            console.log("Stored states:", stateTable.rows);
            expect(stateTable.rows.length).toBeGreaterThan(0);
        });
        
        it("Should mock a complete bridge request", async () => {
            const bridgeScope = 123;
            
            // Set array length first
            const lengthKey = createStorageKey(STORAGE_BRIDGE_REQUEST_INDEX);
            const lengthVal = uint256(1); // 1 request
            
            await evmContract.action.setstate({
                scope: bridgeScope,
                key: lengthKey,
                value_low: lengthVal.value_low,
                value_high: lengthVal.value_high
            }, [{ actor: evmAccount.name, permission: "active" }]);
            
            console.log("✓ Array length set");
            
            // Calculate request array base slot (keccak256 of storage slot)
            const slotBytes = Buffer.from(createStorageKey(STORAGE_BRIDGE_REQUEST_INDEX), 'hex');
            const requestSlot = keccak256(slotBytes);
            
            console.log("Request base slot:", requestSlot);
            
            // Property 0: call_id = 1
            const idVal = uint256(1);
            await evmContract.action.setstate({
                scope: bridgeScope,
                key: addToKey(requestSlot, 0),
                value_low: idVal.value_low,
                value_high: idVal.value_high
            }, [{ actor: evmAccount.name, permission: "active" }]);
            console.log("✓ Call ID set");
            
            // Property 1: sender address
            const senderVal = addressToUint256("0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb");
            await evmContract.action.setstate({
                scope: bridgeScope,
                key: addToKey(requestSlot, 1),
                value_low: senderVal.value_low,
                value_high: senderVal.value_high
            }, [{ actor: evmAccount.name, permission: "active" }]);
            console.log("✓ Sender address set");
            
            // Property 2: tokenId (asset_id)
            const amountVal = uint256("1000000"); // Mock asset_id
            await evmContract.action.setstate({
                scope: bridgeScope,
                key: addToKey(requestSlot, 2),
                value_low: amountVal.value_low,
                value_high: amountVal.value_high
            }, [{ actor: evmAccount.name, permission: "active" }]);
            console.log("✓ Token ID set");
            
            // Property 3: requested_at
            const requestedAtVal = uint256("123456");
            await evmContract.action.setstate({
                scope: bridgeScope,
                key: addToKey(requestSlot, 3),
                value_low: requestedAtVal.value_low,
                value_high: requestedAtVal.value_high
            }, [{ actor: evmAccount.name, permission: "active" }]);
            console.log("✓ Requested_at set");

            // Property 4: collectionName
            const collectionVal = stringToStorageValue("alice.tlos");
            await evmContract.action.setstate({
                scope: bridgeScope,
                key: addToKey(requestSlot, 4),
                value_low: collectionVal.value_low,
                value_high: collectionVal.value_high
            }, [{ actor: evmAccount.name, permission: "active" }]);
            console.log("✓ Collection set");

            // Property 5: receiver = "alice.tlos"
            const receiverVal = stringToStorageValue("alice.tlos");
            await evmContract.action.setstate({
                scope: bridgeScope,
                key: addToKey(requestSlot, 5),
                value_low: receiverVal.value_low,
                value_high: receiverVal.value_high
            }, [{ actor: evmAccount.name, permission: "active" }]);
            console.log("✓ Receiver set");
            
            // Verify data was stored
            const stateTable = await chain.rpc.get_table_rows({
                json: true,
                code: evmAccount.name,
                scope: bridgeScope,
                table: "accountstate"
            });
            
            console.log("Stored states:", stateTable.rows);
            expect(stateTable.rows.length).toBe(7); // length + 6 properties (Request has 6 fields)
        });
    });

    describe(":: RLP Decoding", () => {
        it("Should extract data field from RLP-encoded transaction", async () => {
            // Create an account to use as caller
            const caller = await chain.system.createAccount("caller");
            
            // Set up an account in the accounts table
            await evmContract.action.setaccount({
                index: 1,
                address: "0742d35Cc6634C0532925a3b844Bc9e7595f0bEb",
                account: caller.name
            }, [{ actor: evmAccount.name, permission: "active" }]);

            // Test 1: Simple non-RLP data (backward compatibility)
            const simpleData = Buffer.from("7d056de7000000000000000000000000742d35cc6634c0532925a3b844bc9e7595f0beb", "hex");
            await evmContract.action.raw({
                caller: caller.name,
                tx: Array.from(simpleData),
                estimate: false,
                sender: null
            }, [{ actor: caller.name, permission: "active" }]);

            let lastcall = await chain.rpc.get_table_rows({
                json: true,
                code: evmAccount.name,
                scope: evmAccount.name,
                table: "lastcall"
            });

            expect(lastcall.rows.length).toBe(1);
            expect(lastcall.rows[0].tx_prefix).toBe("7d056de7");
            expect(lastcall.rows[0].caller).toBe(caller.name);
            console.log("✓ Non-RLP data handled correctly");

            // Test 2: RLP-encoded transaction
            // This simulates what nftbridge sends: [nonce, gasPrice, gasLimit, to, value, data, chainId, r, s]
            // For testing, we'll encode a simple transaction with data = "7d056de7..." (bridgeTo function call)
            const rlp = require('rlp');
            
            const nonce = 5;
            const gasPrice = Buffer.from("0000000000000000000000000000000000000000000000000000000000000001", "hex");
            const gasLimit = 250000;
            const to = Buffer.from("0742d35Cc6634C0532925a3b844Bc9e7595f0bEb", "hex");
            const value = 0;
            const data = Buffer.from("7d056de7000000000000000000000000742d35cc6634c0532925a3b844bc9e7595f0beb000000000000000000000000000000000000000000000000000000000000007b", "hex");
            const chainId = 41;
            const r = 0;
            const s = 0;

            const rlpEncoded = rlp.encode([nonce, gasPrice, gasLimit, to, value, data, chainId, r, s]);

            await evmContract.action.raw({
                caller: caller.name,
                tx: Array.from(rlpEncoded),
                estimate: false,
                sender: "0742d35Cc6634C0532925a3b844Bc9e7595f0bEb"
            }, [{ actor: caller.name, permission: "active" }]);

            lastcall = await chain.rpc.get_table_rows({
                json: true,
                code: evmAccount.name,
                scope: evmAccount.name,
                table: "lastcall"
            });

            expect(lastcall.rows.length).toBe(1);
            expect(lastcall.rows[0].tx_prefix).toBe("7d056de7"); // Should extract data field correctly
            expect(lastcall.rows[0].tx_size).toBeGreaterThan(100); // RLP encoded size
            console.log("✓ RLP-encoded transaction decoded correctly");
            console.log("  - tx_size:", lastcall.rows[0].tx_size, "bytes");
            console.log("  - tx_prefix:", lastcall.rows[0].tx_prefix);
        });

        it("Should handle RLP-encoded callbacks (requestSuccessful)", async () => {
            // Create a bridge account
            const bridgeAccount = await chain.system.createAccount("nftbridge");
            
            // Set up the bridge account in accounts table
            await evmContract.action.setaccount({
                index: 2,
                address: "0842d35Cc6634C0532925a3b844Bc9e7595f0bEc",
                account: bridgeAccount.name
            }, [{ actor: evmAccount.name, permission: "active" }]);

            // Set up a request in storage (simulate a pending request)
            const bridgeScope = 2;
            const lengthKey = createStorageKey(STORAGE_BRIDGE_REQUEST_INDEX); // requests.length
            await evmContract.action.setstate({
                scope: bridgeScope,
                key: lengthKey,
                value_low: "1",
                value_high: "0"
            }, [{ actor: evmAccount.name, permission: "active" }]);

            // Create RLP-encoded requestSuccessful(uint256) call
            // Function selector: 0fbc79cd (from EVM_REQUEST_SUCCESSFUL_SIGNATURE)
            const rlp = require('rlp');
            
            const callId = 123;
            const dataHex = "0fbc79cd" + callId.toString(16).padStart(64, '0'); // requestSuccessful(123)
            const data = Buffer.from(dataHex, "hex");
            
            const rlpEncoded = rlp.encode([
                0, // nonce
                Buffer.from("0000000000000000000000000000000000000000000000000000000000000001", "hex"), // gasPrice
                250000, // gasLimit
                Buffer.from("0842d35Cc6634C0532925a3b844Bc9e7595f0bEc", "hex"), // to
                0, // value
                data, // requestSuccessful call data
                41, // chainId
                0, // r
                0  // s
            ]);

            // Send RLP-encoded transaction
            await evmContract.action.raw({
                caller: bridgeAccount.name,
                tx: Array.from(rlpEncoded),
                estimate: false,
                sender: "0842d35Cc6634C0532925a3b844Bc9e7595f0bEc"
            }, [{ actor: bridgeAccount.name, permission: "active" }]);

            // Verify requests array was cleared
            const stateTable = await chain.rpc.get_table_rows({
                json: true,
                code: evmAccount.name,
                scope: bridgeScope,
                table: "accountstate"
            });

            const lengthRow = stateTable.rows.find(r => r.key === lengthKey);
            expect(lengthRow).toBeDefined();
            expect(lengthRow.value_low).toBe("0"); // Should be cleared
            expect(lengthRow.value_high).toBe("0");
            
            console.log("✓ RLP-encoded requestSuccessful() handled correctly");
        });
    });
});
