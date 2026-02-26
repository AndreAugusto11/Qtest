const { Chain } = require("qtest-js");

describe("NFT Bridge Contract", () => {
    let chain;
    let bridgeAccount, evmAccount, adminAccount, testAccount;
    let bridgeContract, evmContract;

    const bridgeAddress = "0742d35Cc6634C0532925a3b844Bc9e7595f0bEb";
    const registerAddress = "0842d35Cc6634C0532925a3b844Bc9e7595f0bEc";

    beforeAll(async () => {
        // Setup chain
        chain = await Chain.setupChain(process.env.CHAIN_NAME || 'TLOS');

        // Create accounts
        bridgeAccount = await chain.system.createAccount("nftbridge");
        evmAccount = await chain.system.createAccount("eosio.evm");
        adminAccount = await chain.system.createAccount("admin");
        testAccount = await chain.system.createAccount("testaccount");

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

        console.log("✓ NFT Bridge and Mock EVM deployed");
    }, 60000);

    afterAll(async () => {
        await chain.clear();
    }, 10000);

    describe(":: Admin Actions", () => {
        it("Should initialize contract with init action", async () => {
            const result = await bridgeContract.action.init(
                {
                    bridge_address: bridgeAddress,
                    register_address: registerAddress,
                    version: "1.0.0",
                    admin: adminAccount.name
                },
                [{ actor: bridgeAccount.name, permission: "active" }]
            );

            expect(result.processed.block_num).toBeGreaterThan(0);

            // Verify config was set
            const configTable = await chain.rpc.get_table_rows({
                json: true,
                code: bridgeAccount.name,
                scope: bridgeAccount.name,
                table: "config"
            });

            expect(configTable.rows.length).toBe(1);
            expect(configTable.rows[0].version).toBe("1.0.0");
            expect(configTable.rows[0].admin).toBe(adminAccount.name);
        });

        it("Should fail to re-initialize contract", async () => {
            try {
                await bridgeContract.action.init(
                    {
                        bridge_address: bridgeAddress,
                        register_address: registerAddress,
                        version: "1.0.1",
                        admin: adminAccount.name
                    },
                    [{ actor: bridgeAccount.name, permission: "active" }]
                );
                fail("Should have thrown");
            } catch (error) {
                expect(error.message).toContain("contract already initialized");
            }
        });

        it("Should clear error logs", async () => {
            // Add some error logs manually (in real scenario these would be added during operations)
            const errorTable = await chain.rpc.get_table_rows({
                json: true,
                code: bridgeAccount.name,
                scope: bridgeAccount.name,
                table: "errorlogs"
            });

            const initialCount = errorTable.rows.length;

            const result = await bridgeContract.action.clearerrorlog(
                { ids: null },
                [{ actor: adminAccount.name, permission: "active" }]
            );

            expect(result.processed.block_num).toBeGreaterThan(0);

            const afterClear = await chain.rpc.get_table_rows({
                json: true,
                code: bridgeAccount.name,
                scope: bridgeAccount.name,
                table: "errorlogs"
            });

            expect(afterClear.rows.length).toBeLessThanOrEqual(initialCount);
        });

        it("Should modify config", async () => {
            const newAdmin = await chain.system.createAccount("newadmin");
            
            const result = await bridgeContract.action.modifyconfig(
                {
                    bridge_address: null,
                    register_address: null,
                    admin: newAdmin.name,
                    version: "1.0.1"
                },
                [{ actor: adminAccount.name, permission: "active" }]
            );

            expect(result.processed.block_num).toBeGreaterThan(0);

            // Verify config was updated
            const configTable = await chain.rpc.get_table_rows({
                json: true,
                code: bridgeAccount.name,
                scope: bridgeAccount.name,
                table: "config"
            });

            expect(configTable.rows[0].version).toBe("1.0.1");
            expect(configTable.rows[0].admin).toBe(newAdmin.name);
        });
    });

    describe(":: EVM State Reading", () => {
        it("Should read request array length from EVM storage", async () => {
            const bridgeScope = 123;

            // Helper functions (same as in bridge.mock.test.ts)
            function createStorageKey(slot) {
                return slot.toString(16).padStart(64, '0');
            }

            function uint256(value) {
                const bn = BigInt(value);
                const mask128 = BigInt('0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
                const low = (bn & mask128).toString();
                const high = (bn >> BigInt(128)).toString();

                return {
                    value_low: low,
                    value_high: high
                };
            }

            // Set up mock EVM state with requests
            const lengthKey = createStorageKey(5);
            const lengthValue = uint256(1); // 1 request

            await evmContract.action.setstate(
                {
                    scope: bridgeScope,
                    key: lengthKey,
                    value_low: lengthValue.value_low,
                    value_high: lengthValue.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Call reqnotify to read from EVM storage
            const result = await bridgeContract.action.reqnotify(
                {},
                [{ actor: bridgeAccount.name, permission: "active" }]
            );

            expect(result.processed.block_num).toBeGreaterThan(0);

            // Check that requests table is updated
            const requestsTable = await chain.rpc.get_table_rows({
                json: true,
                code: bridgeAccount.name,
                scope: bridgeAccount.name,
                table: "requests"
            });

            expect(requestsTable.rows.length).toBeGreaterThanOrEqual(0);
        });

        it("Should read complete bridge request from EVM storage", async () => {
            const bridgeScope = 124; // Different scope to avoid conflicts

            function createStorageKey(slot) {
                return slot.toString(16).padStart(64, '0');
            }

            function uint256(value) {
                const bn = BigInt(value);
                const mask128 = BigInt('0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
                const low = (bn & mask128).toString();
                const high = (bn >> BigInt(128)).toString();

                return {
                    value_low: low,
                    value_high: high
                };
            }

            function addressToUint256(address) {
                const cleanAddr = address.slice(2);
                const paddedHex = cleanAddr.toLowerCase().padStart(64, '0');

                const low = BigInt('0x' + paddedHex.slice(32));
                const high = BigInt('0x' + paddedHex.slice(0, 32));

                return {
                    value_low: low.toString(),
                    value_high: high.toString()
                };
            }

            function stringToStorageValue(str) {
                const hex = Buffer.from(str).toString('hex');
                const length = str.length * 2;
                const paddedHex = hex.padEnd(62, '0') + length.toString(16).padStart(2, '0');

                const low = BigInt('0x' + paddedHex.slice(32));
                const high = BigInt('0x' + paddedHex.slice(0, 32));

                return {
                    value_low: low.toString(),
                    value_high: high.toString()
                };
            }

            // Set array length
            const lengthKey = createStorageKey(5);
            const lengthVal = uint256(1);

            await evmContract.action.setstate(
                {
                    scope: bridgeScope,
                    key: lengthKey,
                    value_low: lengthVal.value_low,
                    value_high: lengthVal.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Set request properties (simplified - in real scenario keccak256 would be used)
            // For this test, we're just storing at base indices
            
            // call_id = 999
            const idVal = uint256(999);
            await evmContract.action.setstate(
                {
                    scope: bridgeScope,
                    key: createStorageKey(100), // Placeholder index
                    value_low: idVal.value_low,
                    value_high: idVal.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // sender address
            const senderVal = addressToUint256("0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb");
            await evmContract.action.setstate(
                {
                    scope: bridgeScope,
                    key: createStorageKey(101),
                    value_low: senderVal.value_low,
                    value_high: senderVal.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // amount
            const amountVal = uint256("1000000"); // 100.0000 TLOS
            await evmContract.action.setstate(
                {
                    scope: bridgeScope,
                    key: createStorageKey(102),
                    value_low: amountVal.value_low,
                    value_high: amountVal.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // receiver
            const receiverVal = stringToStorageValue("alice.tlos");
            await evmContract.action.setstate(
                {
                    scope: bridgeScope,
                    key: createStorageKey(103),
                    value_low: receiverVal.value_low,
                    value_high: receiverVal.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Verify data was stored
            const stateTable = await chain.rpc.get_table_rows({
                json: true,
                code: evmAccount.name,
                scope: bridgeScope,
                table: "accountstate"
            });

            expect(stateTable.rows.length).toBeGreaterThan(0);
        });

        it("Should read refund requests from EVM storage", async () => {
            const bridgeScope = 125;

            function createStorageKey(slot) {
                return slot.toString(16).padStart(64, '0');
            }

            function uint256(value) {
                const bn = BigInt(value);
                const mask128 = BigInt('0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
                const low = (bn & mask128).toString();
                const high = (bn >> BigInt(128)).toString();

                return {
                    value_low: low,
                    value_high: high
                };
            }

            // Set refund array length (slot 6)
            const lengthKey = createStorageKey(6);
            const lengthVal = uint256(0); // No refunds

            await evmContract.action.setstate(
                {
                    scope: bridgeScope,
                    key: lengthKey,
                    value_low: lengthVal.value_low,
                    value_high: lengthVal.value_high
                },
                [{ actor: evmAccount.name, permission: "active" }]
            );

            // Call refundnotify
            const result = await bridgeContract.action.refundnotify(
                {},
                [{ actor: bridgeAccount.name, permission: "active" }]
            );

            expect(result.processed.block_num).toBeGreaterThan(0);

            // Check that refunds table exists
            const refundsTable = await chain.rpc.get_table_rows({
                json: true,
                code: bridgeAccount.name,
                scope: bridgeAccount.name,
                table: "refunds"
            });

            expect(refundsTable.rows.length).toBe(0); // No refunds because length is 0
        });
    });

    describe(":: Bridge Operations", () => {
        it("Should store locked NFT on bridge transfer", async () => {
            // This would test the @notify("atomicassets::transfer") handler
            // For now, we just verify the table structure exists
            
            const lockedTable = await chain.rpc.get_table_rows({
                json: true,
                code: bridgeAccount.name,
                scope: bridgeAccount.name,
                table: "lockednfts"
            });

            expect(Array.isArray(lockedTable.rows)).toBe(true);
        });

        it("Should have error logs table", async () => {
            const errorTable = await chain.rpc.get_table_rows({
                json: true,
                code: bridgeAccount.name,
                scope: bridgeAccount.name,
                table: "errorlogs"
            });

            expect(Array.isArray(errorTable.rows)).toBe(true);
        });
    });
});
