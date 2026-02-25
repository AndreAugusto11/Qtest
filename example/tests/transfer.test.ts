const { Chain } = require("qtest-js");

function findNotifyTrace(transaction, receiverName) {
    const traces = (transaction && transaction.processed && transaction.processed.action_traces) || [];

    const stack = [...traces];
    while (stack.length) {
        const trace = stack.shift();
        if (trace && trace.receiver === receiverName) {
            return trace;
        }
        const inline = trace && trace.inline_traces ? trace.inline_traces : [];
        stack.push(...inline);
    }

    return null;
}

describe("Token Transfer Notification Test", () => {
    let chain;
    let tokenAccount, receiverAccount, testAccount;
    let tokenContract, receiverContract;

    beforeAll(async () => {
        // Setup chain
        chain = await Chain.setupChain(process.env.CHAIN_NAME || 'TLOS');

        // Create accounts
        tokenAccount = await chain.system.createAccount("token");
        receiverAccount = await chain.system.createAccount("receiver");
        testAccount = await chain.system.createAccount("testaccount1");
        await tokenAccount.addCode('active');

        // Deploy token contract
        tokenContract = await tokenAccount.setContract({
            abi: "./build/eosio.token.abi",
            wasm: "./build/eosio.token.wasm",
        });
        
        // Deploy receiver contract
        receiverContract = await receiverAccount.setContract({
            abi: "./build/receiver.abi",
            wasm: "./build/receiver.wasm",
        });
        
        // Create token
        await tokenContract.action.create({
            issuer: tokenAccount.name,
            maximum_supply: "1000000.0000 TLOS"
        }, [{ actor: tokenAccount.name, permission: "active" }]);
        
        // Issue tokens to test account
        await tokenContract.action.issue({
            to: tokenAccount.name,
            quantity: "1000.0000 TLOS",
            memo: "initial issue"
        }, [{ actor: tokenAccount.name, permission: "active" }]);

        console.log("✓ Token and Receiver contracts deployed, token created and issued to token account");
        
        // Transfer to test account
        await tokenContract.action.transfer({
            from: tokenAccount.name,
            to: testAccount.name,
            quantity: "500.0000 TLOS",  // Give enough tokens
            memo: "funding test account"
        }, [{ actor: tokenAccount.name, permission: "active" }]);
        console.log("✓ Test account funded");

        const balanceResult = await chain.rpc.get_table_rows({
            json: true,
            code: tokenAccount.name,
            scope: testAccount.name,
            table: "accounts"
        });
        console.log("Balance check:", balanceResult.rows);
        expect(balanceResult.rows[0].balance).toContain("500.0000 TLOS");
        
        // print success message
        console.log("Setup complete: Token and Receiver contracts deployed, test account funded.");

    }, 60000);

    describe(":: on_notify transfer", () => {
        it("Should trigger on_transfer when tokens sent to receiver", async () => {
            // Transfer tokens to receiver contract
            const result = await tokenContract.action.transfer({
                from: testAccount.name,
                to: receiverAccount.name,
                quantity: "10.0000 TLOS",
                memo: "test memo"
            }, [{ actor: testAccount.name, permission: "active" }]);  // ← Authorization from sender

            const notifyTrace = findNotifyTrace(result, receiverAccount.name);
            expect(notifyTrace).not.toBeNull();
            const consoleOutput = notifyTrace.console || "";

            // Check that our message was printed
            expect(consoleOutput).toContain("Hello, World!");
            expect(consoleOutput).toContain("10.0000 TLOS");
            expect(consoleOutput).toContain("testaccount1");
            expect(consoleOutput).toContain("test memo");
        });

        it("Should NOT trigger when tokens sent to another account", async () => {
            // Create another account
            const otherAccount = await chain.system.createAccount("otheracct");
            
            // Transfer tokens to other account
            const result = await tokenContract.action.transfer({
                from: testAccount.name,
                to: otherAccount.name,
                quantity: "10.0000 TLOS",
                memo: "should not trigger"
            }, [{ actor: testAccount.name, permission: "active" }]);

            const notifyTrace = findNotifyTrace(result, receiverAccount.name);
            expect(notifyTrace).toBeNull();  // Should not find a trace for receiver contract
        });
        
        it("Should handle multiple transfers", async () => {
            // First transfer
            const result1 = await tokenContract.action.transfer({
                from: testAccount.name,
                to: receiverAccount.name,
                quantity: "20.0000 TLOS",
                memo: "first transfer"
            }, [{ actor: testAccount.name, permission: "active" }]);

            // Second transfer
            const result2 = await tokenContract.action.transfer({
                from: testAccount.name,
                to: receiverAccount.name,
                quantity: "30.0000 TLOS",
                memo: "second transfer"
            }, [{ actor: testAccount.name, permission: "active" }]);

            const trace1 = findNotifyTrace(result1, receiverAccount.name);
            const trace2 = findNotifyTrace(result2, receiverAccount.name);

            expect(trace1).not.toBeNull();
            expect(trace2).not.toBeNull();

            // Both should trigger
            expect((trace1.console || "")).toContain("20.0000 TLOS");
            expect((trace2.console || "")).toContain("30.0000 TLOS");
        });

        // it("Should trigger on_transfer when tokens sent to receiver", async () => {
        //     TO DEBUG THE TRACES
        //     // Print EVERYTHING to see structure
        //     console.log("=== FULL RESULT ===");
        //     console.log(JSON.stringify(result, null, 2));
            
        //     // Check action traces
        //     console.log("=== ACTION TRACES ===");
        //     console.log(JSON.stringify(result.processed.action_traces, null, 2));
            
        //     // Look for inline traces
        //     if (result.processed.action_traces[0].inline_traces) {
        //         console.log("=== INLINE TRACES ===");
        //         result.processed.action_traces[0].inline_traces.forEach((trace, idx) => {
        //             console.log(`Trace ${idx}:`, trace.act.account, trace.act.name);
        //             console.log(`Console:`, trace.console);
        //         });
        //     }
            
        //     const notifyTrace = findNotifyTrace(result, receiverAccount.name);
        //     console.log("=== NOTIFY TRACE ===");
        //     console.log(JSON.stringify(notifyTrace, null, 2));
        // });


        
        it("Should receive correct memo", async () => {
            const testMemo = "0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb";
            
            const result = await tokenContract.action.transfer({
                from: testAccount.name,
                to: receiverAccount.name,
                quantity: "15.0000 TLOS",
                memo: testMemo
            }, [{ actor: testAccount.name, permission: "active" }]);

            const notifyTrace = findNotifyTrace(result, receiverAccount.name);
            expect(notifyTrace).not.toBeNull();
            const consoleOutput = notifyTrace.console || "";
            expect(consoleOutput).toContain(testMemo);
        });
    });
});