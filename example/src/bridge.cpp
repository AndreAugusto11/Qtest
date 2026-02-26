// @author Thomas Cuvillier
// @organization Telos Foundation
// @contract bridge
// @version v1.0

#include "../include/bridge.hpp"
#include "../include/token_structs.hpp"

namespace evm_bridge
{
    //======================== Admin actions ==========================
    // Initialize the contract
    // bridge_address - EVM Bridge contract address, after it's deployed
    // register_address - EVM PairBridgeRegister contract address, after it's deployed
    // version - contract version ?? (doesn't matter), can be changed
    // admin - EOSIO account name of the admin, who can change the contract settings, can be changed
    [[eosio::action]]
    void tokenbridge::init(eosio::checksum160 bridge_address, eosio::checksum160 register_address, std::string version, eosio::name admin){
        // Authenticate
        require_auth(get_self());

        // Validate
        check(!config_bridge.exists(), "contract already initialized");
        check(is_account(admin), "initial admin account doesn't exist");

        // Initialize
        auto stored = config_bridge.get_or_create(get_self(), config_row);

        stored.version              = version;
        stored.admin                = admin;
        stored.evm_bridge_address   = bridge_address;
        stored.evm_register_address = register_address;
        stored.max_fee_percentage   = DEFAULT_FEE_PERCENTAGE;
        stored.min_transfer_qty     = DEFAULT_MIN_TRASNFER_QTY;
        stored.min_fee_qty          = DEFAULT_MIN_FEE_QTY;
        stored.blockbastards_reserve_account = BLOCKBASTARDSRESERVEACCOUNT;
        stored.fee_whitelist        = {BLOCKBASTARDSPREMINEACCOUNT};

        // Get the scope
        account_table accounts(EVM_SYSTEM_CONTRACT, EVM_SYSTEM_CONTRACT.value);
        auto accounts_byaddress = accounts.get_index<"byaddress"_n>();
        auto account_bridge = accounts_byaddress.find(pad160(bridge_address));
        auto account_register = accounts_byaddress.find(pad160(register_address));

        stored.evm_bridge_scope = (account_bridge != accounts_byaddress.end()) ? account_bridge->index : 0;
        check(stored.evm_bridge_scope > 0, "Could not find the EVM TokenBridge eosio.evm index");
        stored.evm_register_scope = (account_register != accounts_byaddress.end()) ? account_register->index : 0;
        check(stored.evm_register_scope > 0, "Could not find the EVM PairRegister eosio.evm index");

        config_bridge.set(stored, get_self());
    };

    // Modify any configuration
    [[eosio::action]]
    void tokenbridge::modifyconfig(
        std::optional<eosio::checksum160> bridge_address,
        std::optional<eosio::checksum160> register_address,
        std::optional<eosio::name> admin,
        std::optional<std::string> version,
        std::optional<uint64_t> max_fee_percentage,
        std::optional<uint64_t> min_transfer_qty,
        std::optional<uint64_t> min_fee_qty,
        std::optional<eosio::name> blockbastards_reserve_account,
        std::optional<std::vector<eosio::name>> fee_whitelist
    ){
        require_auth(config_bridge.get().admin);
        auto stored = config_bridge.get();

        if(bridge_address.has_value()){
            // Get the relevant accounts for eosio.evm accountstates table
            account_table accounts(EVM_SYSTEM_CONTRACT, EVM_SYSTEM_CONTRACT.value);
            auto accounts_byaddress = accounts.get_index<"byaddress"_n>();
            auto account_bridge = accounts_byaddress.find(pad160(bridge_address.value()));

            // Save
            stored.evm_bridge_address = bridge_address.value();
            stored.evm_bridge_scope = (account_bridge != accounts_byaddress.end()) ? account_bridge->index : 0;
            check(stored.evm_bridge_scope > 0, "Could not find the EVM TokenBridge eosio.evm index");
        }

        if(register_address.has_value()){
            // Get the relevant accounts for eosio.evm accountstates table
            account_table accounts(EVM_SYSTEM_CONTRACT, EVM_SYSTEM_CONTRACT.value);
            auto accounts_byaddress = accounts.get_index<"byaddress"_n>();
            auto account_register = accounts_byaddress.find(pad160(register_address.value()));

            // Save
            stored.evm_register_address = register_address.value();
            stored.evm_register_scope = (account_register != accounts_byaddress.end()) ? account_register->index : 0;
            check(stored.evm_register_scope > 0, "Could not find the EVM PairBridgeRegister eosio.evm index");
        }

        if(admin.has_value()){ 
            check(is_account(admin.value()), "New admin account does not exist");
            stored.admin = admin.value();
        }

        if(version.has_value()){
            stored.version = version.value();
        }

        if(max_fee_percentage.has_value()){
            check( max_fee_percentage.value() >= 0 
                && max_fee_percentage.value() <= 100,
                "Invalid max fee percentage");
            stored.max_fee_percentage = max_fee_percentage.value();
        }

        if(min_transfer_qty.has_value()){
            check( min_transfer_qty.value() >= 0, "min_transfer_qty must be positive" );
            stored.min_transfer_qty = min_transfer_qty.value();
        }

        if(min_fee_qty.has_value()){
            check( min_fee_qty.value() >= 0, "min_fee_qty must be positive" );
            stored.min_transfer_qty = min_fee_qty.value();
        }

        if(blockbastards_reserve_account.has_value()){
            check(is_account(blockbastards_reserve_account.value()), "Invalid blockbastards_reserve_account value");
            stored.blockbastards_reserve_account = blockbastards_reserve_account.value();
        }

        if(fee_whitelist.has_value()){
            for(const auto& n : fee_whitelist.value()) {
                check(is_account(n), "fee_whitelist contains an invalid account: " + n.to_string());
            }
            stored.fee_whitelist = fee_whitelist.value();
        }
        
        config_bridge.set(stored, get_self());
    }
    
    [[eosio::action]]
    void tokenbridge::clearerrorlog(std::optional<std::vector<uint64_t>> ids) {
        require_auth(config_bridge.get().admin);

        errorlogs_table errorlogs(get_self(), get_self().value);

        if (!ids.has_value()) { // Clear all error logs
            auto itr = errorlogs.begin();
            while (itr != errorlogs.end()) {
                itr = errorlogs.erase(itr);
            }
        } else { // Clear only specified IDs
            for (auto id : ids.value()) {
                auto itr = errorlogs.find(id);
                if (itr != errorlogs.end()) {
                    errorlogs.erase(itr);
                }
            }
        }
    }

    //======================== Token Bridge actions ========================
    // When you transfer QUDO to bridge contract. Catches ANY token transfer TO the bridge (tokens stay locked in bridge contract)
    [[eosio::on_notify("*::transfer")]]
    void tokenbridge::bridge(eosio::name from, eosio::name to, eosio::asset quantity, std::string memo)
    {
        if(from == get_self()) return; // Return so we don't stop the transfer from this contract when bridging from tEVM
        check(to == get_self(), "Recipient is not this contract");
        check(memo.length() == 42, "Memo needs to contain the 42 character EVM recipient address");

        // Open bridge config singleton
        auto conf = config_bridge.get();

        // Check amount
        check(quantity.amount >= 0, "Transfer amount must be positive"); // This should never happen
        uint256_t amount = uint256_t(quantity.amount);
        check( quantity.amount >= conf.min_transfer_qty, "Minimum amount is not reached" );

        // calculate and deduct the fee
        uint64_t fee = calc_fee( from, amount );
        check(amount >= fee, "Fee exceeds transfer amount"); // This should never happen
        amount -= fee;

        // Open EVM config singleton
        auto evm_conf = config.get();

        // Find the EVM account of this contract
        account_table _accounts(EVM_SYSTEM_CONTRACT, EVM_SYSTEM_CONTRACT.value);
        auto accounts_byaccount = _accounts.get_index<"byaccount"_n>();
        auto evm_account = accounts_byaccount.require_find(get_self().value, ("EVM account not found for " + std::string(CONTRACT_NAME)).c_str());        

        // Define EVM Account State table with EVM register contract scope
        account_state_table register_account_states(EVM_SYSTEM_CONTRACT, conf.evm_register_scope);
        auto register_account_states_bykey = register_account_states.get_index<"bykey"_n>();

        // Get array slot to find Pair pairs[] array length
        auto pair_storage_key = toChecksum256(STORAGE_REGISTER_PAIR_INDEX);
        auto pair_array_length = register_account_states_bykey.require_find(pair_storage_key, "No pairs have been found in the EVM register");
        auto pair_array_slot = checksum256ToValue(keccak_256(pair_storage_key.extract_as_byte_array()));
        auto pair_property_count = 10;

        // Get each member of the Pair pairs[] array's antelope_account and compare to get the EVM address
        std::string pair_evm_address = "";
        vector<uint8_t> pair_evm_address_bs;
        uint64_t pair_evm_decimals;
        uint64_t pair_antelope_decimals;
        for(uint64_t i = 0; i < pair_array_length->value; i++){
            // Get the account name string from EVM Storage, this works only for < 32bytes string which any EOSIO name should be (< 13 chars)
            const auto account_name_checksum = register_account_states_bykey.find( getArrayMemberSlot(pair_array_slot, 6, pair_property_count, i) );
            eosio::name account_name = parseNameFromStorage(account_name_checksum->value);
            if(account_name.value  == get_first_receiver().value){
                const auto pair_active = register_account_states_bykey.require_find(getArrayMemberSlot(pair_array_slot, 0, pair_property_count, i), "Unable to find Token Pair");
                check(pair_active->value == uint256_t(1), "This token's pair is paused");
                const auto pair_evm_address_stored = register_account_states_bykey.require_find(getArrayMemberSlot(pair_array_slot, 2, pair_property_count, i), "Unable to find Pair EVM Address");
                pair_evm_address_bs = parseAddressFromStorage(pair_evm_address_stored->value);
                pair_evm_decimals       = static_cast<uint64_t>(register_account_states_bykey.require_find(getArrayMemberSlot(pair_array_slot, 3, pair_property_count, i), "Failed to read EVM decimals")->value);
                pair_antelope_decimals  = static_cast<uint64_t>(register_account_states_bykey.require_find(getArrayMemberSlot(pair_array_slot, 4, pair_property_count, i), "Failed to read antelope decimals")->value);
                break;
            }
        }
        check(pair_evm_address_bs.size() > 0, "This token has no pair registered on this bridge");

        // Prepare address for EVM Bridge call
        auto evm_contract = conf.evm_bridge_address.extract_as_byte_array();
        std::vector<uint8_t> evm_to;
        evm_to.insert(evm_to.end(),  evm_contract.begin(), evm_contract.end());

        // Prepare EVM function signature & arguments
        std::vector<uint8_t> data;
        auto fnsig = checksum256ToValue(eosio::checksum256(toBin(EVM_BRIDGE_SIGNATURE)));
        vector<uint8_t> fnsig_bs = intx::to_byte_string(fnsig);
        fnsig_bs.resize(16);
        data.insert(data.end(), fnsig_bs.begin(), fnsig_bs.end());
        data.insert(data.end(), pair_evm_address_bs.begin(), pair_evm_address_bs.end());

        // Receiver EVM address from memo
        memo.replace(0, 2, ""); // remove the Ox
        auto receiver_ba = pad160(eosio::checksum160( toBin(memo))).extract_as_byte_array();
        std::vector<uint8_t> receiver(receiver_ba.begin(), receiver_ba.end());
        receiver = pad(receiver, 32, true);
        data.insert(data.end(),  receiver.begin(), receiver.end());

        // Amount
        // TODO: if pair_antelope_decimals > pair_evm_decimals, we need to divide the amount by 10^(pair_antelope_decimals - pair_evm_decimals)
        check(pair_evm_decimals >= pair_antelope_decimals, "EVM Decimals have to be no shorter than Antelope decimals");
        vector<uint8_t> amount_bs = pad(intx::to_byte_string(amount * pow (10, pair_evm_decimals - pair_antelope_decimals)), 32, true);
        data.insert(data.end(),  amount_bs.begin(), amount_bs.end());

        // Sender
        std::string sender = from.to_string();
        insertElementPositions(&data, 128); // Our string position
        insertString(&data, sender, sender.length());

        // call TokenBridge.bridgeTo(address token, address receiver, uint amount) on EVM using eosio.evm
        action(
            permission_level {get_self(), "active"_n},
            EVM_SYSTEM_CONTRACT,
            "raw"_n,
            std::make_tuple(get_self(), rlp::encode(evm_account->nonce, evm_conf.gas_price, BRIDGE_GAS, evm_to, uint256_t(0), data, CURRENT_CHAIN_ID, 0, 0),  false, std::optional<eosio::checksum160>(evm_account->address))
        ).send();

        // send the fee to
        if(fee > 0){
            asset transfer_fee = asset(fee, symbol(QUDO));
            name bb_reserve_acc = conf.blockbastards_reserve_account;
            action(
                //permission_level{get_self(), CODEPERMISSION},
                permission_level{get_self(), "active"_n},
                TOKENCONTRACT,
                "transfer"_n,
                std::make_tuple(get_self(), bb_reserve_acc, transfer_fee, std::string(BRIDGEFEEMEMO))
            ).send();
        }
    };

    // Refunds bridge request to EVM if minting reverted on EVM
    [[eosio::action]]
    void tokenbridge::refundnotify()
    {
        // Open config singletons
        auto conf = config_bridge.get();
        auto evm_conf = config.get();

        // Find the EVM account of this contract
        account_table _accounts(EVM_SYSTEM_CONTRACT, EVM_SYSTEM_CONTRACT.value);
        auto accounts_byaccount = _accounts.get_index<"byaccount"_n>();
        auto account = accounts_byaccount.require_find(get_self().value, "Account not found");

        // Clean out old processed refunds
        refunds_table refunds(get_self(), get_self().value);
        auto refunds_by_timestamp = refunds.get_index<"timestamp"_n>();
        auto upper = refunds_by_timestamp.upper_bound(current_time_point().sec_since_epoch() - 60); // remove 60s so we get only requests that are at least 1mn old
        uint64_t count = 10; // max 10 refunds so we never overload CPU
        for(auto itr = refunds_by_timestamp.begin(); count > 0 && itr != upper; count--) {
            itr = refunds_by_timestamp.erase(itr);
        }

        // Define EVM Account State table with EVM bridge contract scope
        account_state_table bridge_account_states(EVM_SYSTEM_CONTRACT, conf.evm_bridge_scope);
        auto bridge_account_states_bykey = bridge_account_states.get_index<"bykey"_n>();

        // Define EVM Account State table with EVM register contract scope
        account_state_table register_account_states(EVM_SYSTEM_CONTRACT, conf.evm_register_scope);
        auto register_account_states_bykey = register_account_states.get_index<"bykey"_n>();

        // Get array slot to find Refund refunds[] array length
        auto refund_storage_key = toChecksum256(STORAGE_BRIDGE_REFUND_INDEX);
        auto refund_array_length = bridge_account_states_bykey.require_find(refund_storage_key, "No refunds found");
        auto refund_array_slot = checksum256ToValue(keccak_256(refund_storage_key.extract_as_byte_array()));
        uint8_t refund_property_count = 6;

        // Prepare address for callback
        auto evm_contract = conf.evm_bridge_address.extract_as_byte_array();
        std::vector<uint8_t> to;
        to.insert(to.end(), evm_contract.begin(), evm_contract.end());

        const auto fnsig = toBin(EVM_REFUND_CALLBACK_SIGNATURE);
        const std::string memo = "Bridge refund";

        auto refunds_by_call_id = refunds.get_index<"callid"_n>();

        // Todo: optimize max (i<2)
        uint64_t y_max = 10;    //  To safe-guard 30ms CPU usage
        uint64_t i_max = 2;     //  increment if 'continue'
        for(uint64_t i = 0; i < refund_array_length->value && i < i_max && i < y_max; i++){
            const auto refund_id_checksum = bridge_account_states_bykey.find(getArrayMemberSlot(refund_array_slot, 0, refund_property_count, i));
            const uint256_t refund_id = (refund_id_checksum != bridge_account_states_bykey.end()) ? refund_id_checksum->value : uint256_t(0); // Needed because row is not set at all if the value is 0
            // Check refund not already being processed
            auto exists = refunds_by_call_id.find(toChecksum256(refund_id));
            if(exists != refunds_by_call_id.end()){
                ++i_max;
                continue;
            }
            const auto refund_id_bs = pad(intx::to_byte_string(refund_id), 16, true);
            const eosio::name receiver = parseNameFromStorage(bridge_account_states_bykey.require_find(getArrayMemberSlot(refund_array_slot, 4, refund_property_count, i), "Failed to read receiver")->value);
            const eosio::name token_account_name = parseNameFromStorage(bridge_account_states_bykey.require_find(getArrayMemberSlot(refund_array_slot, 2, refund_property_count, i), "Failed to read token account")->value);
            const eosio::symbol_code antelope_symbol = parseSymbolCodeFromStorage(bridge_account_states_bykey.require_find(getArrayMemberSlot(refund_array_slot, 3, refund_property_count, i), "Failed to read antelope symbot")->value);
            const uint64_t evmDecimals = static_cast<uint64_t>(bridge_account_states_bykey.require_find(getArrayMemberSlot(refund_array_slot, 5, refund_property_count, i), "Failed to read EVM decimals")->value);

            // Get token from token stat table (and not EVM Register, in case the token issuer changes precision)
            eosio_tokens token_row(token_account_name, antelope_symbol.raw());
            const auto antelope_token = token_row.require_find(antelope_symbol.raw(), "Token not found. Make sure the symbol is correct.");

            // Get amount according to decimal places on each chain
            uint256_t amount = bridge_account_states_bykey.find(getArrayMemberSlot(refund_array_slot, 1, refund_property_count, i))->value;
            if(evmDecimals < antelope_token->supply.symbol.precision()){
                const double exponent = (evmDecimals - antelope_token->supply.symbol.precision()) * 1.0;
                amount = amount / pow(10.0, exponent);
            } else {
                const double exponent = (antelope_token->supply.symbol.precision() - evmDecimals) * 1.0;
                amount = amount * pow(10.0, exponent);
            }
            const uint64_t amount_64 = static_cast<uint64_t>(amount);
            const eosio::asset quantity = asset(amount_64, antelope_token->supply.symbol);

            // Add refund
            refunds.emplace(get_self(), [&](auto& r) {
                r.refund_id = refunds.available_primary_key();
                r.call_id = toChecksum256(refund_id);
                r.timestamp = current_time_point();
            });

            // Send tokens to receiver
            action(
                permission_level{ get_self(), "active"_n },
                    token_account_name,
                    "transfer"_n,
                    std::make_tuple(get_self(), receiver, quantity, memo)
            ).send();

            std::vector<uint8_t> data;
            data.insert(data.end(), fnsig.begin(), fnsig.end());
            data.insert(data.end(), refund_id_bs.begin(), refund_id_bs.end());

            // Send refundSuccessful call to EVM using eosio.evm
            action(
                permission_level {get_self(), "active"_n},
                EVM_SYSTEM_CONTRACT,
                "raw"_n,
                std::make_tuple(get_self(), rlp::encode(account->nonce, evm_conf.gas_price, REFUND_CB_GAS, to, uint256_t(0), data, 41, 0, 0),  false, std::optional<eosio::checksum160>(account->address))
            ).send();
        }

    }

    // Trustless bridge from tEVM
    [[eosio::action]]
    void tokenbridge::reqnotify()
    {
        // Open config singletons
        auto conf = config_bridge.get();
        auto evm_conf = config.get();

        // Find the EVM account of this contract
        account_table _accounts(EVM_SYSTEM_CONTRACT, EVM_SYSTEM_CONTRACT.value);
        auto accounts_byaccount = _accounts.get_index<"byaccount"_n>();
        auto evm_account = accounts_byaccount.require_find(get_self().value, ("EVM account not found for " + std::string(CONTRACT_NAME)).c_str());        

        // Erase old requests
        requests_table requests(get_self(), get_self().value);
        auto requests_by_timestamp = requests.get_index<"timestamp"_n>();
        auto upper = requests_by_timestamp.upper_bound(current_time_point().sec_since_epoch() - 60); // remove 60s so we get only requests that are at least 1mn old
        uint64_t count = 10; // max 10 requests to remove so we never overload CPU
        for(auto itr = requests_by_timestamp.begin(); count > 0 && itr != upper; count--) {
            itr = requests_by_timestamp.erase(itr);
        }

        // Define EVM Account State table with EVM bridge contract scope
        account_state_table bridge_account_states(EVM_SYSTEM_CONTRACT, conf.evm_bridge_scope);
        auto bridge_account_states_bykey = bridge_account_states.get_index<"bykey"_n>();

        // Define EVM Account State table with EVM register contract scope
        account_state_table register_account_states(EVM_SYSTEM_CONTRACT, conf.evm_register_scope);
        auto register_account_states_bykey = register_account_states.get_index<"bykey"_n>();

        // Get array slot to find the TokenBridge Request[] requests array length
        auto request_storage_key = toChecksum256(STORAGE_BRIDGE_REQUEST_INDEX);
        auto request_array_length = bridge_account_states_bykey.require_find(request_storage_key, "No requests found");
        auto request_array_slot = checksum256ToValue(keccak_256(request_storage_key.extract_as_byte_array()));
        uint8_t request_property_count = 8;

        // Prepare address & function signature for callback
        auto evm_contract = conf.evm_bridge_address.extract_as_byte_array();
        std::vector<uint8_t> evm_to;
        evm_to.insert(evm_to.end(), evm_contract.begin(), evm_contract.end());
        auto fnsig = toBin(EVM_SUCCESS_CALLBACK_SIGNATURE);

        // Loop over the requests
        int64_t i_safe_max = 14; // CPU care   -   to be decremented when continue, depending on the step it does so
        int64_t i_max = 2; // only do 2 per call, should smooth things and ensure CPU usage under 30ms
        for(uint64_t i = 0; i < request_array_length->value && i < i_max && i < i_safe_max; i++){
            const auto call_id_checksum = bridge_account_states_bykey.require_find(getArrayMemberSlot(request_array_slot, 0, request_property_count, i), "Failed to read ID");
            const uint256_t call_id = (call_id_checksum != bridge_account_states_bykey.end()) ? call_id_checksum->value : uint256_t(0); // Needed because row is not set at all if the value is 0
            const eosio::name token_account_name = parseNameFromStorage(bridge_account_states_bykey.require_find(getArrayMemberSlot(request_array_slot, 4, request_property_count, i), "Failed to read token account")->value);
            const eosio::symbol_code antelope_symbol = parseSymbolCodeFromStorage(bridge_account_states_bykey.require_find(getArrayMemberSlot(request_array_slot, 5, request_property_count, i), "Failed to read antelope symbol")->value);

            // Get token from token stat table (and not EVM Register, in case the token issuer changes precision)
            eosio_tokens token_row(token_account_name, antelope_symbol.raw());
            auto antelope_token = token_row.find(antelope_symbol.raw());
            if(antelope_token == token_row.end()){
                errorlogs_table errorlogs(get_self(), get_self().value);
                // check errorlogs for call_id and if error_msg start with req
                auto errorlog_by_call_id = errorlogs.get_index<"callid"_n>();
                eosio::checksum256 ci256 = toChecksum256(call_id);
                auto exists = errorlog_by_call_id.find(ci256);
                bool has_req = false;
                // has to iterate in case it could exist (could be an 'if', in scenarios with gt 2 cases, here only refund/request)
                while(exists != errorlog_by_call_id.end()){
                    // It does not start with "req "
                    if(exists->call_id != ci256) // just in case
                        break;
                    if(exists->error_msg.rfind("req ", 0) != 0) { 
                        has_req = true;
                        break;
                    }
                    ++exists;
                }
                if(!has_req){
                    errorlogs.emplace(get_self(), [&](auto& row) {
                        row.id = errorlogs.available_primary_key();
                        row.call_id = ci256;
                        row.error_msg = "req Token not found for symbol: " + antelope_symbol.to_string();
                        row.timestamp = eosio::current_time_point();
                    });
                }
                --i_safe_max;
                ++i_max;
                continue;
            }

            const uint64_t evmDecimals = static_cast<uint64_t>(bridge_account_states_bykey.require_find(getArrayMemberSlot(request_array_slot, 7, request_property_count, i), "Failed to read EVM decimals")->value);
            const eosio::name receiver = parseNameFromStorage(bridge_account_states_bykey.require_find(getArrayMemberSlot(request_array_slot, 6, request_property_count, i), "Failed to read receiver")->value);
            const auto sender_address_checksum = bridge_account_states_bykey.require_find(getArrayMemberSlot(request_array_slot, 1, request_property_count, i), "Failed to read sender address");
            std::string sender_address = bin2hex(parseAddressFromStorage(sender_address_checksum->value));
            const std::string memo = "Sent from tEVM by 0x" + sender_address;
            const vector<uint8_t> call_id_bs = pad(intx::to_byte_string(call_id), 16, true);

            // We made sure on the tEVM side that the max precision for bridging matches antelope and that the wei amount to bridge (minus precision) is =< uint64_t max of 18446744073709551615
            uint256_t amount = bridge_account_states_bykey.require_find(getArrayMemberSlot(request_array_slot, 2, request_property_count, i), "Failed to read amount")->value;
            if(evmDecimals > antelope_token->supply.symbol.precision()){
                const double exponent = (evmDecimals - antelope_token->supply.symbol.precision()) * 1.0;
                amount = amount / pow(10.0, exponent);
            } else {
                const double exponent = (antelope_token->supply.symbol.precision() - evmDecimals) * 1.0;
                amount = amount * pow(10.0, exponent);
            }
            uint64_t amount_64 = static_cast<uint64_t>(amount);
            const eosio::asset quantity = asset(amount_64, antelope_token->supply.symbol);

            // Check request not already being processed
            auto requests_by_call_id = requests.get_index<"callid"_n>();
            auto exists = requests_by_call_id.find(toChecksum256(call_id));
            if(exists != requests_by_call_id.end()){
                i_safe_max -= 2;
                ++i_max;
                continue;
            }

            // Add request
            requests.emplace(get_self(), [&](auto& r) {
                r.request_id = requests.available_primary_key();
                r.call_id = toChecksum256(call_id);
                r.timestamp = current_time_point();
            });

            // Send tokens to receiver
            action(
                permission_level{ get_self(), "active"_n },
                    token_account_name,
                    "transfer"_n,
                    std::make_tuple(get_self(), receiver, quantity, memo)
            ).send();

            // Setup success callback call so request get deleted on tEVM
            std::vector<uint8_t> data;
            data.insert(data.end(), fnsig.begin(), fnsig.end());
            data.insert(data.end(), call_id_bs.begin(), call_id_bs.end());

            // Call success callback on tEVM using eosio.evm
            action(
               permission_level {get_self(), "active"_n},
               EVM_SYSTEM_CONTRACT,
               "raw"_n,
               std::make_tuple(get_self(), rlp::encode(evm_account->nonce + i, evm_conf.gas_price, SUCCESS_CB_GAS, evm_to, uint256_t(0), data, CURRENT_CHAIN_ID, 0, 0),  false, std::optional<eosio::checksum160>(evm_account->address))
            ).send();

            i_safe_max -= 3;
        }
    };
}