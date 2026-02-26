const { Chain } = require("qtest-js");
const { keccak256 } = require('js-sha3');

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
            
            // Storage slot 5 = requests array length
            console.log("Setting mock request array length in EVM storage...");
            const lengthKey = createStorageKey(5);
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
            const lengthKey = createStorageKey(5);
            const lengthVal = uint256(1); // 1 request
            
            await evmContract.action.setstate({
                scope: bridgeScope,
                key: lengthKey,
                value_low: lengthVal.value_low,
                value_high: lengthVal.value_high
            }, [{ actor: evmAccount.name, permission: "active" }]);
            
            console.log("✓ Array length set");
            
            // Calculate request array base slot (keccak256 of slot 5)
            const slotBytes = Buffer.from(createStorageKey(5), 'hex');
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
            
            // Property 2: amount = 100.0000 TLOS (4 decimals)
            const amountVal = uint256("1000000"); // 100.0000 with 4 decimals
            await evmContract.action.setstate({
                scope: bridgeScope,
                key: addToKey(requestSlot, 2),
                value_low: amountVal.value_low,
                value_high: amountVal.value_high
            }, [{ actor: evmAccount.name, permission: "active" }]);
            console.log("✓ Amount set");
            
            // Property 6: receiver = "alice.tlos"
            const receiverVal = stringToStorageValue("alice.tlos");
            await evmContract.action.setstate({
                scope: bridgeScope,
                key: addToKey(requestSlot, 6),
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
            expect(stateTable.rows.length).toBe(5); // length + 4 properties
        });
    });
});

// Helper functions
function createStorageKey(slot) {
    // Convert slot number to checksum256 (64 hex chars)
    const hex = slot.toString(16).padStart(64, '0');
    return hex;
}

function uint256(value) {
    // Split into low and high 128 bits
    const bn = BigInt(value);
    const mask128 = BigInt('0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
    const low = (bn & mask128).toString();  // Return as decimal string
    const high = (bn >> BigInt(128)).toString();  // Return as decimal string
    
    return {
        value_low: low,
        value_high: high
    };
}

function addToKey(baseKey, offset) {
    // baseKey is hex string without 0x
    // Add offset and return as checksum256 (64 hex chars)
    const bn = BigInt('0x' + baseKey) + BigInt(offset);
    return bn.toString(16).padStart(64, '0');
}

function addressToUint256(address) {
    // Convert 0x... address to uint256
    // Address is 20 bytes (40 hex chars), pad to 32 bytes (64 hex chars)
    const cleanAddr = address.slice(2); // Remove 0x
    const paddedHex = cleanAddr.toLowerCase().padStart(64, '0');
    
    // Split into low/high 128 bits
    const low = BigInt('0x' + paddedHex.slice(32)); // Last 32 hex chars
    const high = BigInt('0x' + paddedHex.slice(0, 32)); // First 32 hex chars
    
    return {
        value_low: low.toString(), // Decimal string
        value_high: high.toString() // Decimal string
    };
}

function stringToStorageValue(str) {
    // Encode string for storage (< 32 bytes inline)
    const hex = Buffer.from(str).toString('hex');
    const length = str.length * 2; // length encoding
    const paddedHex = hex.padEnd(62, '0') + length.toString(16).padStart(2, '0');
    
    // Split into low/high
    const low = BigInt('0x' + paddedHex.slice(32));
    const high = BigInt('0x' + paddedHex.slice(0, 32));
    
    return {
        value_low: low.toString(),
        value_high: high.toString()
    };
}