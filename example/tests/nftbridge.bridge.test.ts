const { Chain } = require("qtest-js");
const { keccak256 } = require("js-sha3");

// EVM Storage Slot Constants (must match Solidity contract storage layout)
const STORAGE_REGISTER_PAIR_INDEX = 4;      // PairBridgeNFTRegister: pairs array
const STORAGE_REGISTER_REQUEST_INDEX = 5;   // PairBridgeNFTRegister: requests array
const STORAGE_BRIDGE_REQUEST_INDEX = 5;     // TokenBridgeNFT: requests array
const STORAGE_BRIDGE_REFUND_INDEX = 6;      // TokenBridgeNFT: refunds array

describe("NFT Bridge - Bridge Function", () => {
    let chain;
    let bridgeAccount, evmAccount, adminAccount, atomicAccount, userAccount;
    let bridgeContract, evmContract, atomicContract;

    const bridgeAddress = "0742d35Cc6634C0532925a3b844Bc9e7595f0bEb";
    const registerAddress = "0842d35Cc6634C0532925a3b844Bc9e7595f0bEc";
    const evmRecipient = "0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb0";
    const bridgeScope = 123;
    const registerScope = 124;

    beforeAll(async () => {
        // Setup chain
        chain = await Chain.setupChain(process.env.CHAIN_NAME || 'TLOS');

        // Create accounts
        bridgeAccount = await chain.system.createAccount("nftbridge");
        evmAccount = await chain.system.createAccount("eosio.evm");
        adminAccount = await chain.system.createAccount("admin");
        atomicAccount = await chain.system.createAccount("testcol");
        userAccount = await chain.system.createAccount("alice");

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
        atomicContract = await atomicAccount.setContract({
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

    describe(":: Setup EVM Register with NFT Pair", () => {
        it("Should set up NFT pair in EVM register", async () => {
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

            // Set pairs array length
            const pairsLengthKey = createStorageKey(STORAGE_REGISTER_PAIR_INDEX);
            const pairsLength = uint256(1); // 1 pair

            await evmContract.action.setstate(
                {
                    scope: registerScope,
                    key: pairsLengthKey,
                    value_low: pairsLength.value_low,
                    value_high: pairsLength.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Calculate base slot for pairs array
            const pairsBaseSlot = keccak256ArraySlot(4);
            
            // Set up pair 0 properties (8 properties per pair)
            const propertyCount = 8;
            const pairIndex = 0;

            // Property 0: active = true (1)
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

            // Property 3: antelopeIssuerName
            const issuerKey = (BigInt('0x' + pairsBaseSlot) + BigInt(3 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const issuerValue = stringToStorageValue("testcol");
            await evmContract.action.setstate(
                { scope: registerScope, key: issuerKey, value_low: issuerValue.value_low, value_high: issuerValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 4: antelopeAccountName (collection name)
            const accountKey = (BigInt('0x' + pairsBaseSlot) + BigInt(4 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const accountValue = stringToStorageValue("testcol");
            await evmContract.action.setstate(
                { scope: registerScope, key: accountKey, value_low: accountValue.value_low, value_high: accountValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 5: antelopeSymbolName
            const symbolKey = (BigInt('0x' + pairsBaseSlot) + BigInt(5 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const symbolValue = stringToStorageValue("TESTNFT");
            await evmContract.action.setstate(
                { scope: registerScope, key: symbolKey, value_low: symbolValue.value_low, value_high: symbolValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 6: evmSymbol
            const evmSymbolKey = (BigInt('0x' + pairsBaseSlot) + BigInt(6 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const evmSymbolValue = stringToStorageValue("TNFT");
            await evmContract.action.setstate(
                { scope: registerScope, key: evmSymbolKey, value_low: evmSymbolValue.value_low, value_high: evmSymbolValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Property 7: evmName
            const evmNameKey = (BigInt('0x' + pairsBaseSlot) + BigInt(7 + propertyCount * pairIndex)).toString(16).padStart(64, '0');
            const evmNameValue = stringToStorageValue("Test NFT");
            await evmContract.action.setstate(
                { scope: registerScope, key: evmNameKey, value_low: evmNameValue.value_low, value_high: evmNameValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            console.log("✓ EVM register configured with NFT pair");
        });
    });

    describe(":: Bridge NFT to EVM", () => {
        it("Should mint NFT for user", async () => {
            const result = await atomicContract.action.mint(
                {
                    to: userAccount.name,
                    asset_id: 1,
                    collection: atomicAccount.name,
                    schema: "testschema",
                    immutable_data: [
                        { key: "name", value: "Test NFT #1" },
                        { key: "img", value: "QmTest123" }
                    ],
                    mutable_data: []
                },
                [{ actor: atomicAccount.name, permission: "active" }]
            );

            expect(result.processed.block_num).toBeGreaterThan(0);

            // Verify NFT was minted
            const assetsTable = await chain.rpc.get_table_rows({
                json: true,
                code: atomicAccount.name,
                scope: atomicAccount.name,
                table: "assets"
            });

            expect(assetsTable.rows.length).toBe(1);
            expect(assetsTable.rows[0].asset_id).toBe(1);
            expect(assetsTable.rows[0].owner).toBe(userAccount.name);

            console.log("✓ NFT minted to user");
        });

        it("Should fail to bridge without proper memo", async () => {
            try {
                await atomicContract.action.transfer(
                    {
                        from: userAccount.name,
                        to: bridgeAccount.name,
                        asset_ids: [1],
                        memo: "invalid"
                    },
                    [{ actor: userAccount.name, permission: "active" }]
                );
                throw new Error("Should have thrown");
            } catch (error) {
                expect(error.message).toContain("Memo must be 42-character EVM address");
            }
        });

        it("Should bridge NFT to EVM with valid memo", async () => {
            // First, mint a fresh NFT for this test
            await atomicContract.action.mint(
                {
                    to: userAccount.name,
                    asset_id: 10,
                    collection: atomicAccount.name,
                    schema: "testschema",
                    immutable_data: [{ key: "name", value: "Test NFT #10" }],
                    mutable_data: []
                },
                [{ actor: atomicAccount.name, permission: "active" }]
            );

            const result = await atomicContract.action.transfer(
                {
                    from: userAccount.name,
                    to: bridgeAccount.name,
                    asset_ids: [10],
                    memo: evmRecipient
                },
                [{ actor: userAccount.name, permission: "active" }]
            );

            expect(result.processed.block_num).toBeGreaterThan(0);

            // Verify NFT is now owned by bridge
            const assetsTable = await chain.rpc.get_table_rows({
                json: true,
                code: atomicAccount.name,
                scope: atomicAccount.name,
                table: "assets",
                lower_bound: 10,
                upper_bound: 10
            });

            expect(assetsTable.rows[0].owner).toBe(bridgeAccount.name);

            const lastcall = await chain.rpc.get_table_rows({
                json: true,
                code: evmAccount.name,
                scope: evmAccount.name,
                table: "lastcall"
            });

            expect(lastcall.rows.length).toBe(1);
            expect(lastcall.rows[0].caller).toBe(bridgeAccount.name);
            expect(!!lastcall.rows[0].estimate).toBe(false);
            
            // With RLP encoding, tx_size is the full RLP transaction size
            // It includes: nonce, gasPrice, gasLimit, to, value, data, chainId, r, s
            // The exact size depends on RLP encoding rules, so we just verify it's reasonable
            expect(lastcall.rows[0].tx_size).toBeGreaterThan(100);
            expect(lastcall.rows[0].tx_prefix).toBe("7d056de7");

            console.log("✓ NFT successfully bridged to EVM");
        });

        it("Should fail to bridge multiple NFTs at once", async () => {
            // Mint two more NFTs
            await atomicContract.action.mint(
                {
                    to: userAccount.name,
                    asset_id: 20,
                    collection: atomicAccount.name,
                    schema: "testschema",
                    immutable_data: [{ key: "name", value: "Test NFT #20" }],
                    mutable_data: []
                },
                [{ actor: atomicAccount.name, permission: "active" }]
            );

            await atomicContract.action.mint(
                {
                    to: userAccount.name,
                    asset_id: 21,
                    collection: atomicAccount.name,
                    schema: "testschema",
                    immutable_data: [{ key: "name", value: "Test NFT #21" }],
                    mutable_data: []
                },
                [{ actor: atomicAccount.name, permission: "active" }]
            );

            try {
                await atomicContract.action.transfer(
                    {
                        from: userAccount.name,
                        to: bridgeAccount.name,
                        asset_ids: [20, 21],
                        memo: evmRecipient
                    },
                    [{ actor: userAccount.name, permission: "active" }]
                );
                throw new Error("Should have thrown");
            } catch (error) {
                expect(error.message).toContain("Can only bridge one NFT at a time");
            }
        });
    });

    describe(":: Refund NFT from EVM", () => {
        it("Should refund NFT back to owner", async () => {
            // Mint and transfer NFT to bridge
            await atomicContract.action.mint(
                {
                    to: userAccount.name,
                    asset_id: 30,
                    collection: atomicAccount.name,
                    schema: "testschema",
                    immutable_data: [{ key: "name", value: "Test NFT #30" }],
                    mutable_data: []
                },
                [{ actor: atomicAccount.name, permission: "active" }]
            );

            await atomicContract.action.transfer(
                {
                    from: userAccount.name,
                    to: bridgeAccount.name,
                    asset_ids: [30],
                    memo: evmRecipient
                },
                [{ actor: userAccount.name, permission: "active" }]
            );

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

            function keccak256ArraySlot(slot) {
                const slotHex = slot.toString(16).padStart(64, '0');
                const hash = keccak256(Buffer.from(slotHex, 'hex'));
                return hash.padStart(64, '0');
            }

            // Refund array length
            const refundsLengthKey = createStorageKey(STORAGE_BRIDGE_REFUND_INDEX);
            const refundsLength = uint256(1);
            await evmContract.action.setstate(
                {
                    scope: bridgeScope,
                    key: refundsLengthKey,
                    value_low: refundsLength.value_low,
                    value_high: refundsLength.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            const refundsBaseSlot = keccak256ArraySlot(6);
            const propertyCount = 4;
            const refundIndex = 0;

            const refundIdKey = (BigInt('0x' + refundsBaseSlot) + BigInt(0 + propertyCount * refundIndex)).toString(16).padStart(64, '0');
            const refundIdValue = uint256(1001);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: refundIdKey, value_low: refundIdValue.value_low, value_high: refundIdValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            const refundAssetKey = (BigInt('0x' + refundsBaseSlot) + BigInt(1 + propertyCount * refundIndex)).toString(16).padStart(64, '0');
            const refundAssetValue = uint256(30);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: refundAssetKey, value_low: refundAssetValue.value_low, value_high: refundAssetValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            const refundOwnerKey = (BigInt('0x' + refundsBaseSlot) + BigInt(2 + propertyCount * refundIndex)).toString(16).padStart(64, '0');
            const refundOwnerValue = stringToStorageValue(userAccount.name);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: refundOwnerKey, value_low: refundOwnerValue.value_low, value_high: refundOwnerValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            const refundCollectionKey = (BigInt('0x' + refundsBaseSlot) + BigInt(3 + propertyCount * refundIndex)).toString(16).padStart(64, '0');
            const refundCollectionValue = stringToStorageValue(atomicAccount.name);
            await evmContract.action.setstate(
                { scope: bridgeScope, key: refundCollectionKey, value_low: refundCollectionValue.value_low, value_high: refundCollectionValue.value_high },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            const result = await bridgeContract.action.refundnotify({}, [{ actor: bridgeAccount.name, permission: "active" }]);
            expect(result.processed.block_num).toBeGreaterThan(0);

            const assetsTable = await chain.rpc.get_table_rows({
                json: true,
                code: atomicAccount.name,
                scope: atomicAccount.name,
                table: "assets",
                lower_bound: 30,
                upper_bound: 30
            });

            expect(assetsTable.rows[0].owner).toBe(userAccount.name);

            const refundsTable = await chain.rpc.get_table_rows({
                json: true,
                code: bridgeAccount.name,
                scope: bridgeAccount.name,
                table: "refunds"
            });

            expect(refundsTable.rows.length).toBe(1);
            expect(refundsTable.rows[0].refund_id).toBe(1001);
            expect(refundsTable.rows[0].asset_id).toBe(30);
            expect(refundsTable.rows[0].owner).toBe(userAccount.name);
            expect(refundsTable.rows[0].collection).toBe(atomicAccount.name);

            // Calling again should not duplicate or re-transfer
            await bridgeContract.action.refundnotify({}, [{ actor: bridgeAccount.name, permission: "active" }]);
            const refundsTableAfter = await chain.rpc.get_table_rows({
                json: true,
                code: bridgeAccount.name,
                scope: bridgeAccount.name,
                table: "refunds"
            });
            expect(refundsTableAfter.rows.length).toBe(1);
        });
    });
});
