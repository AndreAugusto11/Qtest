const { Chain } = require("qtest-js");

describe("Mock AtomicAssets Contract", function () {
    let chain;
    let atomicAccount;
    let alice;
    let bob;

    let atomicContract;

    beforeAll(async function () {
        chain = await Chain.setupChain(process.env.CHAIN_NAME || 'TLOS');

        // IMPORTANT: ≤12 characters, lowercase only
        atomicAccount = await chain.system.createAccount("atomicassets");
        alice = await chain.system.createAccount("alice");
        bob = await chain.system.createAccount("bob");

        // Deploy contract
        atomicContract = await atomicAccount.setContract({
            wasm: "./build/mock.atomic.wasm",
            abi: "./build/mock.atomic.abi",
        });
    });

    afterAll(async function () {
        await chain.clear();
    });

    describe("mint()", function () {
        it("should mint an NFT to alice", async function () {
            
            console.log("Minting asset to alice...");
            await atomicContract.action.mint({
                to: "alice",
                asset_id: 1,
                collection: "coolcol",
                schema: "schema1",
                immutable_data: [
                    { key: "name", value: "Cool NFT #1" },
                    { key: "img", value: "QmHash123..." },
                    { key: "rarity", value: "legendary" }
                ],
                mutable_data: [
                    { key: "level", value: "1" },
                    { key: "xp", value: "0" }
                ]
            }, [{ actor: atomicAccount.name, permission: "active" }]);

            console.log("Minted asset to alice");

            const assetsTable = await chain.rpc.get_table_rows({
                json: true,
                code: atomicAccount.name,
                scope: atomicAccount.name,
                table: "assets",
            });

            console.log("Stored states:", assetsTable.rows);
            expect(assetsTable.rows.length).toBeGreaterThan(0);
            expect(assetsTable.rows[0].owner).toBe("alice");
            expect(assetsTable.rows[0].asset_id).toBe(1);
            expect(assetsTable.rows[0].collection).toBe("coolcol");
            expect(assetsTable.rows[0].schema).toBe("schema1");
            
            // Verify immutable data
            expect(assetsTable.rows[0].immutable_data).toBeDefined();
            expect(assetsTable.rows[0].immutable_data.length).toBe(3);
            expect(assetsTable.rows[0].immutable_data[0].key).toBe("name");
            expect(assetsTable.rows[0].immutable_data[0].value).toBe("Cool NFT #1");
            expect(assetsTable.rows[0].immutable_data[2].value).toBe("legendary");
            
            // Verify mutable data
            expect(assetsTable.rows[0].mutable_data).toBeDefined();
            expect(assetsTable.rows[0].mutable_data.length).toBe(2);
            expect(assetsTable.rows[0].mutable_data[0].key).toBe("level");
            expect(assetsTable.rows[0].mutable_data[1].value).toBe("0");
        });
    });

    describe("transfer()", function () {
        it("should transfer NFT from alice to bob", async function () {
            console.log("Transferring asset from alice to bob...");
            await atomicContract.action.transfer({
                from: "alice",
                to: "bob",
                asset_ids: [1],
                memo: "Enjoy your new NFT!"
            }, [{ actor: "alice", permission: "active" }]);

            console.log("Transferred asset from alice to bob");

            const assetsTable = await chain.rpc.get_table_rows({
                json: true,
                code: atomicAccount.name,
                scope: atomicAccount.name,
                table: "assets",
            });

            console.log("Stored states after transfer:", assetsTable.rows);
            expect(assetsTable.rows.length).toBeGreaterThan(0);
            expect(assetsTable.rows[0].owner).toBe("bob");
        });

        it("should fail if non-owner tries to transfer", async function () {
            try {
                console.log("Attempting unauthorized transfer (alice trying to transfer bob's NFT)...");
                await atomicContract.action.transfer({
                    from: "alice",
                    to: "bob",
                    asset_ids: [1],
                    memo: "Trying to transfer someone else's NFT!"
                }, [{ actor: "alice", permission: "active" }]);
            } catch (e) {
                console.log("Caught expected error:", e.message);
                expect(e.message).toContain("Not asset owner");
                return;
            }
            throw new Error("Unauthorized transfer did not fail as expected");
        });

        it("should fail if asset does not exist", async function () {
            try {
                console.log("Attempting to transfer non-existent asset...");
                await atomicContract.action.transfer({
                    from: "bob",
                    to: "alice",
                    asset_ids: [999],
                    memo: "Trying to transfer a non-existent NFT!"
                }, [{ actor: "bob", permission: "active" }]);
            } catch (e) {
                console.log("Caught expected error:", e.message);
                expect(e.message).toContain("Asset not found");
                return;
            }
            throw new Error("Transfer of non-existent asset did not fail as expected");
        });
    });
});