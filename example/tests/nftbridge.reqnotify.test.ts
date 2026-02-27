const { Chain } = require("qtest-js");
const { keccak256 } = require("js-sha3");

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

            // Helper functions for EVM storage
            function uint256(value) {
                const bn = BigInt(value);
                const mask128 = BigInt('0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
                const low = (bn & mask128).toString();
                const high = (bn >> BigInt(128)).toString();
                return { value_low: low, value_high: high };
            }

            function stringToStorageValue(str) {
                const hex = Buffer.from(str).toString('hex');
                const length = str.length * 2;
                const paddedHex = hex.padEnd(62, '0') + length.toString(16).padStart(2, '0');
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

            // Register NFT pair in EVM register (slot 4)
            // The notify_nft action looks for pairs at storage key 4 (STORAGE_REGISTER_PAIR_INDEX)
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
            const pairsBaseSlot = keccak256ArraySlot(4);
            const propertyCount = 8;
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

            // Property 3: antelopeIssuerName (collection)
            const issuerKey = (BigInt('0x' + pairsBaseSlot) + BigInt(3 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const issuerValue = stringToStorageValue("mycollect");
            await evmContract.action.setstate(
                { scope: registerScope, key: issuerKey, value_low: issuerValue.value_low, value_high: issuerValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 4: antelopeAccountName (collection account)
            const accountKey = (BigInt('0x' + pairsBaseSlot) + BigInt(4 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const accountValue = stringToStorageValue("mycollect");
            await evmContract.action.setstate(
                { scope: registerScope, key: accountKey, value_low: accountValue.value_low, value_high: accountValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 5: antelopeSymbolName
            const symbolKey = (BigInt('0x' + pairsBaseSlot) + BigInt(5 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const symbolValue = stringToStorageValue("MYNFT");
            await evmContract.action.setstate(
                { scope: registerScope, key: symbolKey, value_low: symbolValue.value_low, value_high: symbolValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 6: evmSymbol
            const evmSymbolKey = (BigInt('0x' + pairsBaseSlot) + BigInt(6 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const evmSymbolValue = stringToStorageValue("MYNFT");
            await evmContract.action.setstate(
                { scope: registerScope, key: evmSymbolKey, value_low: evmSymbolValue.value_low, value_high: evmSymbolValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 7: evmName
            const evmNameKey = (BigInt('0x' + pairsBaseSlot) + BigInt(7 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
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
                const length = str.length * 2;
                const paddedHex = hex.padEnd(62, '0') + length.toString(16).padStart(2, '0');
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

            // Set requests array length (slot 5)
            const requestsLengthKey = createStorageKey(5);
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
            const requestsBaseSlot = keccak256ArraySlot(5);

            // Set up request 0 (8 properties per request)
            const propertyCount = 8;
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

            // Property 3: collection (mycollect)
            const collectionKey = (BigInt('0x' + requestsBaseSlot) + BigInt(3 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            const collectionValue = stringToStorageValue("mycollect");
            await evmContract.action.setstate(
                { scope: bridgeScope, key: collectionKey, value_low: collectionValue.value_low, value_high: collectionValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 4: token_symbol
            const tokenSymbolKey = (BigInt('0x' + requestsBaseSlot) + BigInt(4 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            const tokenSymbolValue = stringToStorageValue("NFT");
            await evmContract.action.setstate(
                { scope: bridgeScope, key: tokenSymbolKey, value_low: tokenSymbolValue.value_low, value_high: tokenSymbolValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 5: receiver (bob)
            const receiverKey = (BigInt('0x' + requestsBaseSlot) + BigInt(5 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            const receiverValue = stringToStorageValue(receiverAccount.name);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: receiverKey, value_low: receiverValue.value_low, value_high: receiverValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 7: evm_decimals (not used for NFTs)
            const decimalsKey = (BigInt('0x' + requestsBaseSlot) + BigInt(7 + propertyCount * requestIndex)).toString(16).padStart(64, '0');
            const decimalsValue = uint256(0);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: decimalsKey, value_low: decimalsValue.value_low, value_high: decimalsValue.value_high },
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

            console.log("✓ NFT request processed and transferred successfully");
        });

        it("Should process multiple NFT requests", async () => {
            // TODO
        });

        it("Should reject duplicate NFT request by call_id", async () => {
            // TODO
        });
    });
});
