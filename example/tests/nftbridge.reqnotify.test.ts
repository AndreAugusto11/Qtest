const { Chain } = require("qtest-js");
const { keccak256 } = require("js-sha3");
const { createStorageKey, uint256, stringToStorageValue, addressToUint256, keccak256ArraySlot } = require('./test-utils');

// EVM Storage Slot Constants (must match Solidity contract storage layout)
const STORAGE_REGISTER_PAIR_INDEX = 4;      // PairBridgeNFTRegister: pairs array
const STORAGE_REGISTER_REQUEST_INDEX = 5;   // PairBridgeNFTRegister: requests array
const STORAGE_BRIDGE_REQUEST_INDEX = 5;     // TokenBridgeNFT: requests array
const STORAGE_BRIDGE_REFUND_INDEX = 6;      // TokenBridgeNFT: refunds array

describe("NFT Bridge - Request Notify (NFT Request Fulfillment)", () => {
    let chain;
    let bridgeAccount, evmAccount, adminAccount, collectionAccount, userAccount, receiverAccount;
    let bridgeContract, evmContract, atomicContract;

    const bridgeAddress = "0742d35Cc6634C0532925a3b844Bc9e7595f0bEb";
    const registerAddress = "0842d35Cc6634C0532925a3b844Bc9e7595f0bEc";
    const bridgeScope = 123;
    const registerScope = 124;

    beforeAll(async () => {
        // Setup chain
        chain = await Chain.setupChain(process.env.CHAIN_NAME || 'TLOS');

        // Create accounts
        bridgeAccount = await chain.system.createAccount("nftbridge");
        evmAccount = await chain.system.createAccount("eosio.evm");
        adminAccount = await chain.system.createAccount("admin");
        collectionAccount = await chain.system.createAccount("mycollect");
        userAccount = await chain.system.createAccount("alice");
        receiverAccount = await chain.system.createAccount("bob");

        // Add eosio.code permission to bridge
        await bridgeAccount.addCode('active');

        // Deploy bridge contract
        bridgeContract = await bridgeAccount.setContract({
            abi: "./build/nftbridge.abi",
            wasm: "./build/nftbridge.wasm",
        });

        // Deploy mock EVM contract
        evmContract = await evmAccount.setContract({
            abi: "./build/mock.evm.abi",
            wasm: "./build/mock.evm.wasm",
        });

        // Deploy mock atomic contract
        atomicContract = await collectionAccount.setContract({
            abi: "./build/mock.atomic.abi",
            wasm: "./build/mock.atomic.wasm"
        });

        console.log("✓ All contracts deployed");

        // Register EVM accounts for bridge/register address lookup
        await evmContract.action.setaccount(
            {
                index: bridgeScope,
                address: bridgeAddress,
                account: bridgeAccount.name
            },
            [{ actor: evmAccount.name, permission: "active" }]
        );

        await evmContract.action.setaccount(
            {
                index: registerScope,
                address: registerAddress,
                account: bridgeAccount.name
            },
            [{ actor: evmAccount.name, permission: "active" }]
        );

        // Initialize bridge
        await bridgeContract.action.init(
            {
                bridge_address: bridgeAddress,
                register_address: registerAddress,
                version: "1.0.0",
                admin: adminAccount.name
            },
            [{ actor: bridgeAccount.name, permission: "active" }]
        );

        console.log("✓ Bridge initialized");
    }, 60000);

    afterAll(async () => {
        await chain.clear();
    }, 10000);

    describe(":: Setup NFT Request Fulfillment", () => {
        it("Should mint NFT for fulfillment", async () => {
            // Mint NFT to a user (asset_id 100)
            await atomicContract.action.mint(
                {
                    to: receiverAccount.name,
                    asset_id: 100,
                    collection: collectionAccount.name,
                    schema: "nftschema",
                    immutable_data: [{ key: "name", value: "Request Fulfill NFT #100" }],
                    mutable_data: []
                },
                [{ actor: collectionAccount.name, permission: "active" }]
            );

            console.log("✓ NFT minted to user");

            // Register NFT pair in EVM register (slot 4)
            // The notify_nft action looks for pairs at STORAGE_REGISTER_PAIR_INDEX
            const pairsLengthKey = '0000000000000000000000000000000000000000000000000000000000000004';
            const pairsLength = uint256(1);

            await evmContract.action.setstate(
                {
                    scope: registerScope,
                    key: pairsLengthKey,
                    value_low: pairsLength.value_low,
                    value_high: pairsLength.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Calculate base slot for pairs array (slot 4)
            const pairsBaseSlot = keccak256ArraySlot(STORAGE_REGISTER_PAIR_INDEX);
            const propertyCount = 7;
            const pairIndex = 0;

            // Property 0: active = 1 (pair is active)
            const activeKey = (BigInt('0x' + pairsBaseSlot) + BigInt(0 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const activeValue = uint256(1);
            await evmContract.action.setstate(
                { scope: registerScope, key: activeKey, value_low: activeValue.value_low, value_high: activeValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 1: id = 1
            const idKey = (BigInt('0x' + pairsBaseSlot) + BigInt(1 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const idValue = uint256(1);
            await evmContract.action.setstate(
                { scope: registerScope, key: idKey, value_low: idValue.value_low, value_high: idValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 2: evmAddress
            const evmAddrKey = (BigInt('0x' + pairsBaseSlot) + BigInt(2 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const evmAddrValue = addressToUint256("0x0123456789abcdef0123456789abcdef01234567");
            await evmContract.action.setstate(
                { scope: registerScope, key: evmAddrKey, value_low: evmAddrValue.value_low, value_high: evmAddrValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 3: collectionName
            const issuerKey = (BigInt('0x' + pairsBaseSlot) + BigInt(3 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const issuerValue = stringToStorageValue("mycollect");
            await evmContract.action.setstate(
                { scope: registerScope, key: issuerKey, value_low: issuerValue.value_low, value_high: issuerValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 4: collectionCreator
            const accountKey = (BigInt('0x' + pairsBaseSlot) + BigInt(4 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const accountValue = stringToStorageValue("mycollect");
            await evmContract.action.setstate(
                { scope: registerScope, key: accountKey, value_low: accountValue.value_low, value_high: accountValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 5: evmSymbol
            const evmSymbolKey = (BigInt('0x' + pairsBaseSlot) + BigInt(5 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const evmSymbolValue = stringToStorageValue("MYNFT");
            await evmContract.action.setstate(
                { scope: registerScope, key: evmSymbolKey, value_low: evmSymbolValue.value_low, value_high: evmSymbolValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 6: evmName
            const evmNameKey = (BigInt('0x' + pairsBaseSlot) + BigInt(6 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const evmNameValue = stringToStorageValue("My NFT");
            await evmContract.action.setstate(
                { scope: registerScope, key: evmNameKey, value_low: evmNameValue.value_low, value_high: evmNameValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            console.log("✓ NFT pair registered in EVM");

            // Transfer NFT to bridge to simulate it being locked for transfer
            await atomicContract.action.transfer(
                {
                    from: receiverAccount.name,
                    to: bridgeAccount.name,
                    asset_ids: [100],
                    memo: "0x0123456789abcdef0123456789abcdef01234567"
                },
                [{ actor: receiverAccount.name, permission: "active" }]
            );

            console.log("✓ NFT transferred to bridge for locking");
        });
    });

    describe(":: Bridge NFT Requests from EVM", () => {
        it("Should process single NFT request (unlock)", async () => {
            // Set requests array length
            const requestsLengthKey = createStorageKey(STORAGE_BRIDGE_REQUEST_INDEX);
            const requestsLength = uint256(1);

            await evmContract.action.setstate(
                {
                    scope: bridgeScope,
                    key: requestsLengthKey,
                    value_low: requestsLength.value_low,
                    value_high: requestsLength.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Calculate base slot for requests array
            const requestsBaseSlot = keccak256ArraySlot(STORAGE_BRIDGE_REQUEST_INDEX);

            // Set up request 0 (6 properties per request)
            const propertyCount = 6;
            const requestIndex = 0;

            // Property 0: call_id = 1001
            const callIdKey = (BigInt('0x' + requestsBaseSlot) + BigInt(0 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            const callIdValue = uint256(1001);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: callIdKey, value_low: callIdValue.value_low, value_high: callIdValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 1: sender (EVM address)
            const senderKey = (BigInt('0x' + requestsBaseSlot) + BigInt(1 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            const senderValue = addressToUint256("0x0123456789abcdef0123456789abcdef01234567");
            await evmContract.action.setstate(
                { scope: bridgeScope, key: senderKey, value_low: senderValue.value_low, value_high: senderValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 2: asset_id = 100 (NFT to transfer)
            const assetIdKey = (BigInt('0x' + requestsBaseSlot) + BigInt(2 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            const assetIdValue = uint256(100);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: assetIdKey, value_low: assetIdValue.value_low, value_high: assetIdValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 3: requested_at (timestamp)
            const requestedAtKey = (BigInt('0x' + requestsBaseSlot) + BigInt(3 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            const requestedAtValue = uint256(123456);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: requestedAtKey, value_low: requestedAtValue.value_low, value_high: requestedAtValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 4: collection (mycollect)
            const collectionKey = (BigInt('0x' + requestsBaseSlot) + BigInt(4 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            const collectionValue = stringToStorageValue("mycollect");
            await evmContract.action.setstate(
                { scope: bridgeScope, key: collectionKey, value_low: collectionValue.value_low, value_high: collectionValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 5: receiver (bob)
            const receiverKey = (BigInt('0x' + requestsBaseSlot) + BigInt(5 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            const receiverValue = stringToStorageValue(receiverAccount.name);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: receiverKey, value_low: receiverValue.value_low, value_high: receiverValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Call reqnotify
            const result = await bridgeContract.action.reqnotify({}, [{ actor: bridgeAccount.name, permission: "active" }]);
            expect(result.processed.block_num).toBeGreaterThan(0);

            // Verify NFT was transferred back to receiver (i.e. unlocked)
            const assetsTable = await chain.rpc.get_table_rows({
                json: true,
                code: collectionAccount.name,
                scope: collectionAccount.name,
                table: "assets",
                lower_bound: 100,
                upper_bound: 100
            });

            expect(assetsTable.rows.length).toBe(1);
            expect(assetsTable.rows[0].owner).toBe(receiverAccount.name);

            // Verify EVM requests array is empty after processing
            const evmStatesTable = await chain.rpc.get_table_rows({
                json: true,
                code: evmAccount.name,
                scope: bridgeScope,
                table: "accountstate",
                lower_bound: requestsLengthKey,
                upper_bound: requestsLengthKey,
                key_type: 'sha256',
                index_position: 2
            });

            // The requests array length should be 0 after processing
            expect(evmStatesTable.rows.length).toBe(1);
            expect(evmStatesTable.rows[0].value_low).toBe("0");
            expect(evmStatesTable.rows[0].value_high).toBe("0");

            console.log("✓ NFT request processed and transferred successfully");
            console.log("✓ EVM requests list cleared after processing");
        });

        it("Should process multiple NFT requests", async () => {
            function createStorageKey(slot) {
                return slot.toString(16).padStart(64, '0');
            }

            function uint256(value) {
                const bn = BigInt(value);
                const mask128 = BigInt('0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
                const low = (bn & mask128).toString();
                const high = (bn >> BigInt(128)).toString();
                return { value_low: low, value_high: high };
            }

            function stringToStorageValue(str) {
                const hex = Buffer.from(str).toString('hex');
                const length = str.length;
                const paddedHex = hex.padEnd(62, '0') + (length * 2).toString(16).padStart(2, '0');
                const low = BigInt('0x' + paddedHex.slice(32));
                const high = BigInt('0x' + paddedHex.slice(0, 32));
                return { value_low: low.toString(), value_high: high.toString() };
            }

            function addressToUint256(address) {
                const cleanAddr = address.slice(2).toLowerCase();
                const paddedHex = cleanAddr.padStart(64, '0');
                const low = BigInt('0x' + paddedHex.slice(32));
                const high = BigInt('0x' + paddedHex.slice(0, 32));
                return { value_low: low.toString(), value_high: high.toString() };
            }

            function keccak256ArraySlot(slot) {
                const slotHex = slot.toString(16).padStart(64, '0');
                const hash = keccak256(Buffer.from(slotHex, 'hex'));
                return hash.padStart(64, '0');
            }

            // Mint second NFT (asset_id 101)
            await atomicContract.action.mint(
                {
                    to: bridgeAccount.name,
                    asset_id: 101,
                    collection: collectionAccount.name,
                    schema: "nftschema",
                    immutable_data: [{ key: "name", value: "Request Fulfill NFT #101" }],
                    mutable_data: []
                },
                [{ actor: collectionAccount.name, permission: "active" }]
            );
            // Mint NFT 100 again to bridge (was transferred in previous test)
            await atomicContract.action.mint(
                {
                    to: bridgeAccount.name,
                    asset_id: 102,  // Use 102 since 100 was already used
                    collection: collectionAccount.name,
                    schema: "nftschema",
                    immutable_data: [{ key: "name", value: "Request Fulfill NFT #102" }],
                    mutable_data: []
                },
                [{ actor: collectionAccount.name, permission: "active" }]
            );
            // Set requests array length to 2
            const requestsLengthKey = createStorageKey(STORAGE_BRIDGE_REQUEST_INDEX);
            const requestsLength = uint256(2);

            await evmContract.action.setstate(
                {
                    scope: bridgeScope,
                    key: requestsLengthKey,
                    value_low: requestsLength.value_low,
                    value_high: requestsLength.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Calculate base slot for requests array
            const requestsBaseSlot = keccak256ArraySlot(STORAGE_BRIDGE_REQUEST_INDEX);
            const propertyCount = 6;

            // === Request 0: NFT 102 to bob ===
            let requestIndex = 0;

            // Property 0: call_id = 2001
            let callIdKey = (BigInt('0x' + requestsBaseSlot) + BigInt(0 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            let callIdValue = uint256(2001);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: callIdKey, value_low: callIdValue.value_low, value_high: callIdValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 1: sender
            let senderKey = (BigInt('0x' + requestsBaseSlot) + BigInt(1 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            let senderValue = addressToUint256("0x0123456789abcdef0123456789abcdef01234567");
            await evmContract.action.setstate(
                { scope: bridgeScope, key: senderKey, value_low: senderValue.value_low, value_high: senderValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 2: asset_id = 102
            let assetIdKey = (BigInt('0x' + requestsBaseSlot) + BigInt(2 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            let assetIdValue = uint256(102);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: assetIdKey, value_low: assetIdValue.value_low, value_high: assetIdValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 3: requested_at
            let requestedAtKey = (BigInt('0x' + requestsBaseSlot) + BigInt(3 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            let requestedAtValue = uint256(123456);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: requestedAtKey, value_low: requestedAtValue.value_low, value_high: requestedAtValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 4: collection
            let collectionKey = (BigInt('0x' + requestsBaseSlot) + BigInt(4 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            let collectionValue = stringToStorageValue("mycollect");
            await evmContract.action.setstate(
                { scope: bridgeScope, key: collectionKey, value_low: collectionValue.value_low, value_high: collectionValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 5: receiver = bob
            let receiverKey = (BigInt('0x' + requestsBaseSlot) + BigInt(5 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            let receiverValue = stringToStorageValue(receiverAccount.name);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: receiverKey, value_low: receiverValue.value_low, value_high: receiverValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // === Request 1: NFT 101 to alice ===
            requestIndex = 1;

            // Property 0: call_id = 2002
            callIdKey = (BigInt('0x' + requestsBaseSlot) + BigInt(0 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            callIdValue = uint256(2002);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: callIdKey, value_low: callIdValue.value_low, value_high: callIdValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 1: sender
            senderKey = (BigInt('0x' + requestsBaseSlot) + BigInt(1 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            senderValue = addressToUint256("0xabcdef0123456789abcdef0123456789abcdef01");
            await evmContract.action.setstate(
                { scope: bridgeScope, key: senderKey, value_low: senderValue.value_low, value_high: senderValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 2: asset_id = 101
            assetIdKey = (BigInt('0x' + requestsBaseSlot) + BigInt(2 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            assetIdValue = uint256(101);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: assetIdKey, value_low: assetIdValue.value_low, value_high: assetIdValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 3: requested_at
            requestedAtKey = (BigInt('0x' + requestsBaseSlot) + BigInt(3 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            requestedAtValue = uint256(123457);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: requestedAtKey, value_low: requestedAtValue.value_low, value_high: requestedAtValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 4: collection
            collectionKey = (BigInt('0x' + requestsBaseSlot) + BigInt(4 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            collectionValue = stringToStorageValue("mycollect");
            await evmContract.action.setstate(
                { scope: bridgeScope, key: collectionKey, value_low: collectionValue.value_low, value_high: collectionValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 5: receiver = alice
            receiverKey = (BigInt('0x' + requestsBaseSlot) + BigInt(5 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            receiverValue = stringToStorageValue(userAccount.name);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: receiverKey, value_low: receiverValue.value_low, value_high: receiverValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Call reqnotify to process both requests
            const result = await bridgeContract.action.reqnotify({}, [{ actor: bridgeAccount.name, permission: "active" }]);
            expect(result.processed.block_num).toBeGreaterThan(0);

            // Verify NFT 102 was transferred to bob
            const asset102 = await chain.rpc.get_table_rows({
                json: true,
                code: collectionAccount.name,
                scope: collectionAccount.name,
                table: "assets",
                lower_bound: 102,
                upper_bound: 102
            });
            expect(asset102.rows.length).toBe(1);
            expect(asset102.rows[0].owner).toBe(receiverAccount.name);

            // Verify NFT 101 was transferred to alice
            const asset101 = await chain.rpc.get_table_rows({
                json: true,
                code: collectionAccount.name,
                scope: collectionAccount.name,
                table: "assets",
                lower_bound: 101,
                upper_bound: 101
            });
            expect(asset101.rows.length).toBe(1);
            expect(asset101.rows[0].owner).toBe(userAccount.name);

            // Verify EVM requests array is empty
            const evmStatesTable = await chain.rpc.get_table_rows({
                json: true,
                code: evmAccount.name,
                scope: bridgeScope,
                table: "accountstate",
                lower_bound: requestsLengthKey,
                upper_bound: requestsLengthKey,
                key_type: 'sha256',
                index_position: 2
            });
            expect(evmStatesTable.rows.length).toBe(1);
            expect(evmStatesTable.rows[0].value_low).toBe("0");
            expect(evmStatesTable.rows[0].value_high).toBe("0");

            console.log("✓ Multiple NFT requests processed successfully");
        });

        it("Should handle empty requests array gracefully", async () => {

            // Set requests array length
            const requestsLengthKey = createStorageKey(STORAGE_BRIDGE_REQUEST_INDEX);
            const requestsLength = uint256(0);

            await evmContract.action.setstate(
                {
                    scope: bridgeScope,
                    key: requestsLengthKey,
                    value_low: requestsLength.value_low,
                    value_high: requestsLength.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Call reqnotify - should do nothing gracefully
            const result = await bridgeContract.action.reqnotify({}, [{ actor: bridgeAccount.name, permission: "active" }]);
            expect(result.processed.block_num).toBeGreaterThan(0);

            console.log("✓ Empty requests array handled gracefully");
        });
    });
});
