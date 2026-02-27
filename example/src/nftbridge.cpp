#include "../include/nftbridge.hpp"
#include "../include/constants.hpp"

#include <array>

namespace nft_bridge
{
    namespace {
        constexpr name evm_account = evm_bridge::EVM_SYSTEM_CONTRACT;

        std::array<uint8_t, 32> checksum_to_bytes(const checksum256& value) {
            return value.extract_as_byte_array();
        }

        checksum256 bytes_to_checksum(const std::array<uint8_t, 32>& bytes) {
            return checksum256(bytes);
        }

        checksum256 pad160(const checksum160& input) {
            std::array<uint8_t, 32> output = {};
            auto input_bytes = input.extract_as_byte_array();
            std::copy(std::begin(input_bytes), std::end(input_bytes), std::begin(output) + 12);
            return checksum256(output);
        }

        checksum256 make_storage_key(uint64_t slot) {
            std::array<uint8_t, 32> bytes{};
            for (int i = 0; i < 8; ++i) {
                bytes[31 - i] = static_cast<uint8_t>((slot >> (i * 8)) & 0xFF);
            }
            return bytes_to_checksum(bytes);
        }

        uint64_t uint256_to_uint64(uint128_t low, uint128_t high) {
            check(high == 0, "uint256 value out of range for uint64");
            uint64_t value = static_cast<uint64_t>(low);
            check(static_cast<uint128_t>(value) == low, "uint256 value out of range for uint64");
            return value;
        }

        std::vector<uint8_t> uint256_to_bytes_internal(uint128_t low, uint128_t high) {
            std::vector<uint8_t> bytes(32, 0);
            for (int i = 0; i < 16; ++i) {
                bytes[15 - i] = static_cast<uint8_t>((high >> (i * 8)) & 0xFF);
                bytes[31 - i] = static_cast<uint8_t>((low >> (i * 8)) & 0xFF);
            }
            return bytes;
        }

        std::array<uint8_t, 32> uint256_to_bytes(uint128_t low, uint128_t high) {
            std::array<uint8_t, 32> bytes{};
            for (int i = 0; i < 16; ++i) {
                bytes[15 - i] = static_cast<uint8_t>((high >> (i * 8)) & 0xFF);
                bytes[31 - i] = static_cast<uint8_t>((low >> (i * 8)) & 0xFF);
            }
            return bytes;
        }

        // Convert checksum256 (big-endian 32 bytes) to uint256_t
        uint256_t checksum256_to_uint256(const checksum256& value) {
            auto bytes = checksum_to_bytes(value);
            // Extract high 16 bytes and low 16 bytes as uint128_t
            uint128_t high = 0;
            uint128_t low = 0;
            for (int i = 0; i < 16; ++i) {
                high = (high << 8) | bytes[i];
                low = (low << 8) | bytes[i + 16];
            }
            return uint256_t(high, low);
        }

        std::string bytes_to_hex(const std::vector<uint8_t>& data) {
            static const char* hex = "0123456789abcdef";
            std::string out;
            out.reserve(data.size() * 2);
            for (auto byte : data) {
                out.push_back(hex[(byte >> 4) & 0x0F]);
                out.push_back(hex[byte & 0x0F]);
            }
            return out;
        }

        std::vector<uint8_t> from_hex(const std::string& hex_str) {
            std::vector<uint8_t> result;
            result.reserve(hex_str.size() / 2);
            for (size_t i = 0; i < hex_str.size(); i += 2) {
                uint8_t high = (hex_str[i] >= '0' && hex_str[i] <= '9') ? (hex_str[i] - '0') : (hex_str[i] - 'a' + 10);
                uint8_t low = (hex_str[i+1] >= '0' && hex_str[i+1] <= '9') ? (hex_str[i+1] - '0') : (hex_str[i+1] - 'a' + 10);
                result.push_back((high << 4) | low);
            }
            return result;
        }

        std::string decode_short_string(uint128_t low, uint128_t high) {
            const auto bytes = uint256_to_bytes(low, high);
            const uint8_t length_marker = bytes[31];
            const uint8_t length = static_cast<uint8_t>(length_marker / 2);
            check(length <= 31, "invalid short string length");
            return std::string(reinterpret_cast<const char*>(bytes.data()),
                               reinterpret_cast<const char*>(bytes.data() + length));
        }

        std::string decode_address(uint128_t low, uint128_t high) {
            const auto bytes = uint256_to_bytes(low, high);
            std::vector<uint8_t> addr(bytes.begin() + 12, bytes.end());
            return std::string("0x") + bytes_to_hex(addr);
        }

        bool read_evm_state(uint64_t scope, const checksum256& key, uint128_t& value_low, uint128_t& value_high) {
            account_state_table states(evm_account, scope);
            auto states_bykey = states.get_index<"bykey"_n>();
            auto itr = states_bykey.find(key);
            if (itr == states_bykey.end()) {
                return false;
            }
            value_low = itr->value_low;
            value_high = itr->value_high;
            return true;
        }

        static inline uint64_t rotl64(uint64_t x, uint64_t y) {
            return (x << y) | (x >> (64 - y));
        }

        void keccakf(uint64_t st[25]) {
            static const uint64_t rndc[24] = {
                0x0000000000000001ULL, 0x0000000000008082ULL,
                0x800000000000808aULL, 0x8000000080008000ULL,
                0x000000000000808bULL, 0x0000000080000001ULL,
                0x8000000080008081ULL, 0x8000000000008009ULL,
                0x000000000000008aULL, 0x0000000000000088ULL,
                0x0000000080008009ULL, 0x000000008000000aULL,
                0x000000008000808bULL, 0x800000000000008bULL,
                0x8000000000008089ULL, 0x8000000000008003ULL,
                0x8000000000008002ULL, 0x8000000000000080ULL,
                0x000000000000800aULL, 0x800000008000000aULL,
                0x8000000080008081ULL, 0x8000000000008080ULL,
                0x0000000080000001ULL, 0x8000000080008008ULL
            };

            static const int rotc[24] = {
                1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
                27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44
            };

            static const int piln[24] = {
                10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
                15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1
            };

            for (int round = 0; round < 24; ++round) {
                uint64_t bc[5];
                for (int i = 0; i < 5; ++i) {
                    bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
                }
                for (int i = 0; i < 5; ++i) {
                    uint64_t t = bc[(i + 4) % 5] ^ rotl64(bc[(i + 1) % 5], 1);
                    for (int j = 0; j < 25; j += 5) {
                        st[j + i] ^= t;
                    }
                }

                uint64_t t = st[1];
                for (int i = 0; i < 24; ++i) {
                    int j = piln[i];
                    uint64_t tmp = st[j];
                    st[j] = rotl64(t, rotc[i]);
                    t = tmp;
                }

                for (int j = 0; j < 25; j += 5) {
                    for (int i = 0; i < 5; ++i) {
                        bc[i] = st[j + i];
                    }
                    for (int i = 0; i < 5; ++i) {
                        st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
                    }
                }

                st[0] ^= rndc[round];
            }
        }

        checksum256 keccak256(const std::array<uint8_t, 32>& input) {
            uint64_t st[25] = {0};
            constexpr size_t rate = 136;
            uint8_t* st_bytes = reinterpret_cast<uint8_t*>(st);

            for (size_t i = 0; i < input.size(); ++i) {
                st_bytes[i] ^= input[i];
            }
            st_bytes[input.size()] ^= 0x01;
            st_bytes[rate - 1] ^= 0x80;

            keccakf(st);

            std::array<uint8_t, 32> out{};
            for (size_t i = 0; i < out.size(); ++i) {
                out[i] = st_bytes[i];
            }
            return bytes_to_checksum(out);
        }

        checksum256 add_to_key(const checksum256& base, uint64_t offset) {
            auto bytes = checksum_to_bytes(base);
            uint64_t carry = offset;
            for (int i = 0; i < 32 && carry > 0; ++i) {
                int idx = 31 - i;
                uint64_t sum = static_cast<uint64_t>(bytes[idx]) + (carry & 0xFF);
                bytes[idx] = static_cast<uint8_t>(sum & 0xFF);
                carry = (carry >> 8) + (sum >> 8);
            }
            return bytes_to_checksum(bytes);
        }
    }

    // ======================== Admin Actions ========================
    
    [[eosio::action]]
    void nftbridge::init(checksum160 bridge_address, checksum160 register_address, string version, name admin) {
        require_auth(get_self());
        check(!config_bridge.exists(), "contract already initialized");
        check(is_account(admin), "admin account doesn't exist");

        config_row stored;
        stored.version = version;
        stored.admin = admin;
        stored.evm_bridge_address = bridge_address;
        stored.evm_register_address = register_address;

        // Get EVM scopes
        account_table accounts(evm_account, evm_account.value);
        auto accounts_byaddress = accounts.get_index<"byaddress"_n>();
        auto account_bridge = accounts_byaddress.find(pad160(bridge_address));
        auto account_register = accounts_byaddress.find(pad160(register_address));

        stored.evm_bridge_scope = (account_bridge != accounts_byaddress.end()) ? account_bridge->index : 0;
        check(stored.evm_bridge_scope > 0, "Could not find the EVM Bridge eosio.evm index");
        stored.evm_register_scope = (account_register != accounts_byaddress.end()) ? account_register->index : 0;
        check(stored.evm_register_scope > 0, "Could not find the EVM Register eosio.evm index");

        config_bridge.set(stored, get_self());
    }

    [[eosio::action]]
    void nftbridge::modifyconfig(
        optional<checksum160> bridge_address,
        optional<checksum160> register_address,
        optional<name> admin,
        optional<string> version
    ) {
        auto conf = config_bridge.get();
        require_auth(conf.admin);

        if (bridge_address.has_value()) {
            conf.evm_bridge_address = bridge_address.value();
            account_table accounts(evm_account, evm_account.value);
            auto accounts_byaddress = accounts.get_index<"byaddress"_n>();
            auto account_bridge = accounts_byaddress.find(pad160(conf.evm_bridge_address));
            conf.evm_bridge_scope = (account_bridge != accounts_byaddress.end()) ? account_bridge->index : 0;
            check(conf.evm_bridge_scope > 0, "Could not find the EVM Bridge eosio.evm index");
        }

        if (register_address.has_value()) {
            conf.evm_register_address = register_address.value();
            account_table accounts(evm_account, evm_account.value);
            auto accounts_byaddress = accounts.get_index<"byaddress"_n>();
            auto account_register = accounts_byaddress.find(pad160(conf.evm_register_address));
            conf.evm_register_scope = (account_register != accounts_byaddress.end()) ? account_register->index : 0;
            check(conf.evm_register_scope > 0, "Could not find the EVM Register eosio.evm index");
        }

        if (admin.has_value()) {
            check(is_account(admin.value()), "admin account doesn't exist");
            conf.admin = admin.value();
        }

        if (version.has_value()) {
            conf.version = version.value();
        }

        config_bridge.set(conf, get_self());
    }

    [[eosio::action]]
    void nftbridge::clearerrorlog(optional<vector<uint64_t>> ids) {
        require_auth(config_bridge.get().admin);

        errorlogs_table errorlogs(get_self(), get_self().value);

        if (!ids.has_value()) {
            auto itr = errorlogs.begin();
            while (itr != errorlogs.end()) {
                itr = errorlogs.erase(itr);
            }
        } else {
            for (auto id : ids.value()) {
                auto itr = errorlogs.find(id);
                if (itr != errorlogs.end()) {
                    errorlogs.erase(itr);
                }
            }
        }
    }

    // ======================== Bridge Actions ========================

    [[eosio::on_notify("*::transfer")]]
    void nftbridge::bridge(
        name from,
        name to,
        vector<uint64_t> asset_ids,
        string memo
    ) {
        // Ignore if transferring from this contract (prevents loops when refunding)
        if (from == get_self()) return;
        
        // Must be sent to this contract
        check(to == get_self(), "NFT must be sent to bridge contract");
        
        // Memo must be EVM address (42 chars: 0x + 40 hex)
        check(memo.length() == 42, "Memo must be 42-character EVM address (0x...)");
        check(memo.substr(0, 2) == "0x", "Memo must start with 0x");

        // Only support single NFT transfers for now
        check(asset_ids.size() == 1, "Can only bridge one NFT at a time");
        uint64_t asset_id = asset_ids[0];

        // Get config
        auto conf = config_bridge.get();

        // Get collection name from the contract that sent the transfer notification
        name collection_name = get_first_receiver();
        
        // Read from PairBridgeNFTRegister to get the EVM token address
        vector<uint8_t> pair_evm_address_bs = get_collection_evm_address(collection_name);
        check(pair_evm_address_bs.size() > 0, "This token has no pair registered on this bridge");

        // Prepare address for EVM Bridge call
        auto evm_contract = conf.evm_bridge_address.extract_as_byte_array();
        std::vector<uint8_t> evm_to;
        evm_to.insert(evm_to.end(), evm_contract.begin(), evm_contract.end());

        // Prepare EVM function signature & arguments for bridgeTo(address token, address receiver, uint256 tokenId, string sender)
        std::vector<uint8_t> data;
        
        // Function signature: bridgeTo(address,address,uint256,string)
        // keccak256("bridgeTo(address,address,uint256,string)") = 0x7d056de7... (first 4 bytes)
        vector<uint8_t> fnsig = from_hex(evm_bridge::EVM_NFT_BRIDGE_TO_SIGNATURE);
        data.insert(data.end(), fnsig.begin(), fnsig.end());

        // Parameter 1: token (EVM NFT contract address) - padded to 32 bytes
        vector<uint8_t> token_param(12, 0);
        token_param.insert(token_param.end(), pair_evm_address_bs.begin(), pair_evm_address_bs.end());
        data.insert(data.end(), token_param.begin(), token_param.end());

        // Parameter 2: receiver (EVM address from memo) - padded to 32 bytes
        string receiver_hex = memo.substr(2); // Remove 0x prefix
        vector<uint8_t> receiver_bytes;
        for (size_t i = 0; i < receiver_hex.length(); i += 2) {
            string byte_str = receiver_hex.substr(i, 2);
            receiver_bytes.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
        }
        vector<uint8_t> receiver_param(12, 0);
        receiver_param.insert(receiver_param.end(), receiver_bytes.begin(), receiver_bytes.end());
        data.insert(data.end(), receiver_param.begin(), receiver_param.end());

        // Parameter 3: tokenId (asset_id as uint256) - 32 bytes
        vector<uint8_t> token_id(32, 0);
        for (int i = 7; i >= 0; i--) {
            token_id[24 + i] = static_cast<uint8_t>((asset_id >> (i * 8)) & 0xFF);
        }
        data.insert(data.end(), token_id.begin(), token_id.end());

        // Parameter 4: sender (string, from.to_string())
        // Offset for string parameter (points to where string data starts = 4 * 32 = 128 bytes after params start)
        vector<uint8_t> string_offset(32, 0);
        string_offset[31] = 0x80; // 128 in hex
        data.insert(data.end(), string_offset.begin(), string_offset.end());

        // String data: length + content (padded to 32-byte boundary)
        string sender_str = from.to_string();
        vector<uint8_t> string_length(32, 0);
        string_length[31] = static_cast<uint8_t>(sender_str.length());
        data.insert(data.end(), string_length.begin(), string_length.end());
        
        vector<uint8_t> string_data(sender_str.begin(), sender_str.end());
        // Pad to multiple of 32 bytes
        size_t padding_needed = (32 - (string_data.size() % 32)) % 32;
        string_data.insert(string_data.end(), padding_needed, 0);
        data.insert(data.end(), string_data.begin(), string_data.end());

        // Get EVM config for gas price (if present)
        config_singleton_evm evm_config(evm_account, evm_account.value);
        config evm_conf;
        if (evm_config.exists()) {
            evm_conf = evm_config.get();
        } else {
            evm_conf = config();
        }

        // Find the EVM account of this bridge contract
        account_table _accounts(evm_account, evm_account.value);
        auto accounts_byaccount = _accounts.get_index<"byaccount"_n>();
        auto evm_bridge_account = accounts_byaccount.find(get_self().value);
        check(evm_bridge_account != accounts_byaccount.end(), "EVM account not found for NFT bridge");

        // Call TokenNFTBridge.bridgeTo() on EVM using eosio.evm raw action with RLP encoding
        // Create lvalues for RLP encoding (required by rlp::encode)
        uint64_t nonce = evm_bridge_account->nonce;
        uint256_t gas_price = checksum256_to_uint256(evm_conf.gas_price);
        uint64_t gas_limit = evm_bridge::BRIDGE_GAS;
        uint256_t value = 0;
        uint64_t chain_id = evm_bridge::CURRENT_CHAIN_ID;
        uint64_t r = 0;
        uint64_t s = 0;
        
        action(
            permission_level{get_self(), "active"_n},
            evm_account,
            "raw"_n,
            std::make_tuple(
                get_self(),
                rlp::encode(nonce, gas_price, gas_limit, evm_to, value, data, chain_id, r, s),
                false,
                std::optional<checksum160>(evm_bridge_account->address)
            )
        ).send();

        print("NFT bridged: asset_id=", asset_id, " collection=", collection_name, " to EVM receiver=", memo);
    }

    [[eosio::action]]
    void nftbridge::reqnotify() {
        auto conf = config_bridge.get();
        uint64_t bridge_scope = conf.evm_bridge_scope != 0 ? conf.evm_bridge_scope : 123;

        // Read EVM storage for NFT unlock requests
        uint128_t length_low = 0;
        uint128_t length_high = 0;
        const auto length_key = make_storage_key(evm_bridge::STORAGE_BRIDGE_REQUEST_INDEX);
        if (!read_evm_state(bridge_scope, length_key, length_low, length_high)) {
            print("No request length found in EVM state");
            return;
        }

        const uint64_t length = uint256_to_uint64(length_low, length_high);
        if (length == 0) {
            print("No requests to process");
            return;
        }

        const auto base_slot = keccak256(checksum_to_bytes(length_key));

        for (uint64_t i = 0; i < length; ++i) {
            const auto element_base = add_to_key(base_slot, i * 6);

            uint128_t value_low = 0;
            uint128_t value_high = 0;

            uint64_t call_id = 0;
            uint64_t asset_id = 0;
            std::string sender;
            std::string receiver;
            std::string collection;
            uint64_t requested_at = 0;

            // Property 0: call_id
            if (read_evm_state(bridge_scope, add_to_key(element_base, 0), value_low, value_high)) {
                call_id = uint256_to_uint64(value_low, value_high);
            }

            // Property 1: sender (EVM address)
            if (read_evm_state(bridge_scope, add_to_key(element_base, 1), value_low, value_high)) {
                sender = decode_address(value_low, value_high);
            }

            // Property 2: asset_id (NFT to transfer back)
            if (read_evm_state(bridge_scope, add_to_key(element_base, 2), value_low, value_high)) {
                asset_id = uint256_to_uint64(value_low, value_high);
            }

            // Property 3: requested_at (timestamp)
            if (read_evm_state(bridge_scope, add_to_key(element_base, 3), value_low, value_high)) {
                requested_at = uint256_to_uint64(value_low, value_high);
            }
            (void)requested_at;

            // Property 4: collection (Antelope NFT collection account)
            if (read_evm_state(bridge_scope, add_to_key(element_base, 4), value_low, value_high)) {
                collection = decode_short_string(value_low, value_high);
            }

            // Property 5: receiver (Antelope account that gets the NFT)
            if (read_evm_state(bridge_scope, add_to_key(element_base, 5), value_low, value_high)) {
                receiver = decode_short_string(value_low, value_high);
            }

            // Transfer NFT to receiver
            if (!collection.empty() && !receiver.empty() && asset_id > 0) {
                name collection_name(collection);
                name receiver_name(receiver);

                // Call atomic assets contract to transfer NFT back to receiver
                action(
                    permission_level{get_self(), "active"_n},
                    collection_name,
                    "transfer"_n,
                    std::make_tuple(get_self(), receiver_name, std::vector<uint64_t>{asset_id}, std::string("NFT bridge request fulfilled"))
                ).send();

                // Call EVM bridge contract to confirm request was processed successfully
                // This triggers requestSuccessful(uint id) which deletes the request from EVM storage
                auto evm_contract = conf.evm_bridge_address.extract_as_byte_array();
                std::vector<uint8_t> evm_to;
                evm_to.insert(evm_to.end(), evm_contract.begin(), evm_contract.end());

                // Prepare EVM function call: requestSuccessful(uint256 id)
                // Function selector: keccak256("requestSuccessful(uint256)") first 4 bytes
                std::vector<uint8_t> data;
                vector<uint8_t> fnsig = from_hex(evm_bridge::EVM_REQUEST_SUCCESSFUL_SIGNATURE);
                data.insert(data.end(), fnsig.begin(), fnsig.end());

                // Parameter: id (call_id as uint256) - 32 bytes
                vector<uint8_t> id_param(32, 0);
                for (int j = 7; j >= 0; j--) {
                    id_param[24 + j] = static_cast<uint8_t>((call_id >> (j * 8)) & 0xFF);
                }
                data.insert(data.end(), id_param.begin(), id_param.end());

                // Find the EVM account of this bridge contract
                account_table _accounts(evm_account, evm_account.value);
                auto accounts_byaccount = _accounts.get_index<"byaccount"_n>();
                auto evm_bridge_account = accounts_byaccount.find(get_self().value);
                
                if (evm_bridge_account != accounts_byaccount.end()) {
                    // Get EVM config for gas price
                    config_singleton_evm evm_config(evm_account, evm_account.value);
                    config evm_conf;
                    if (evm_config.exists()) {
                        evm_conf = evm_config.get();
                    } else {
                        evm_conf = config();
                    }

                    // Call requestSuccessful() on EVM bridge contract with RLP encoding
                    // Create lvalues for RLP encoding (required by rlp::encode)
                    uint64_t nonce = evm_bridge_account->nonce;
                    uint256_t gas_price = checksum256_to_uint256(evm_conf.gas_price);
                    uint64_t gas_limit = evm_bridge::SUCCESS_CB_GAS;
                    uint256_t value = 0;
                    uint64_t chain_id = evm_bridge::CURRENT_CHAIN_ID;
                    uint64_t r = 0;
                    uint64_t s = 0;
                    
                    action(
                        permission_level{get_self(), "active"_n},
                        evm_account,
                        "raw"_n,
                        std::make_tuple(
                            get_self(),
                            rlp::encode(nonce, gas_price, gas_limit, evm_to, value, data, chain_id, r, s),
                            false,
                            std::optional<checksum160>(evm_bridge_account->address)
                        )
                    ).send();
                }

                print("Fulfilled request ", call_id, ": transferred NFT ", asset_id, " to ", receiver, "; ");
            }
        }
    }

    [[eosio::action]]
    void nftbridge::refundnotify() {
        auto conf = config_bridge.get();

        // Clean old refunds (optional retention)
        refunds_table refunds(get_self(), get_self().value);
        auto refunds_by_timestamp = refunds.get_index<"timestamp"_n>();
        auto upper = refunds_by_timestamp.upper_bound(current_time_point().sec_since_epoch() - 60);
        uint64_t cleanup_count = 10;
        for (auto itr = refunds_by_timestamp.begin(); cleanup_count > 0 && itr != upper; --cleanup_count) {
            itr = refunds_by_timestamp.erase(itr);
        }

        uint64_t bridge_scope = conf.evm_bridge_scope != 0 ? conf.evm_bridge_scope : 123; // Default for mock tests

        // Read EVM storage for refund requests
        uint128_t length_low = 0;
        uint128_t length_high = 0;
        const auto length_key = make_storage_key(evm_bridge::STORAGE_BRIDGE_REFUND_INDEX);
        if (!read_evm_state(bridge_scope, length_key, length_low, length_high)) {
            print("No refund length found in EVM state");
            return;
        }

        const uint64_t length = uint256_to_uint64(length_low, length_high);
        if (length == 0) {
            print("No refunds to process");
            return;
        }

        const auto base_slot = keccak256(checksum_to_bytes(length_key));

        auto refunds_by_refundid = refunds.get_index<"refundid"_n>();

        for (uint64_t i = 0; i < length; ++i) {
            const auto element_base = add_to_key(base_slot, i * 4);

            uint128_t value_low = 0;
            uint128_t value_high = 0;

            uint64_t refund_id = 0;
            uint64_t asset_id = 0;
            std::string receiver;
            std::string collection;

            if (read_evm_state(bridge_scope, add_to_key(element_base, 0), value_low, value_high)) {
                refund_id = uint256_to_uint64(value_low, value_high);
            }

            if (read_evm_state(bridge_scope, add_to_key(element_base, 1), value_low, value_high)) {
                asset_id = uint256_to_uint64(value_low, value_high);
            }

            if (read_evm_state(bridge_scope, add_to_key(element_base, 2), value_low, value_high)) {
                collection = decode_short_string(value_low, value_high);
            }

            if (read_evm_state(bridge_scope, add_to_key(element_base, 3), value_low, value_high)) {
                receiver = decode_short_string(value_low, value_high);
            }

            if (refund_id == 0 || asset_id == 0 || receiver.empty() || collection.empty()) {
                continue;
            }

            if (refunds_by_refundid.find(refund_id) != refunds_by_refundid.end()) {
                continue;
            }

            // Transfer NFT back to owner
            action(
                permission_level{get_self(), "active"_n},
                name{collection},
                "transfer"_n,
                std::make_tuple(get_self(), name{receiver}, vector<uint64_t>{asset_id}, std::string("Bridge refund"))
            ).send();

            // Call EVM bridge contract to confirm refund was processed successfully
            // This triggers refundSuccessful(uint id) which deletes the refund from EVM storage
            auto evm_contract = conf.evm_bridge_address.extract_as_byte_array();
            std::vector<uint8_t> evm_to;
            evm_to.insert(evm_to.end(), evm_contract.begin(), evm_contract.end());

            // Prepare EVM function call: refundSuccessful(uint256 id)
            // Function selector: keccak256("refundSuccessful(uint256)") first 4 bytes
            std::vector<uint8_t> data;
            vector<uint8_t> fnsig = from_hex(evm_bridge::EVM_REFUND_SUCCESSFUL_SIGNATURE);
            data.insert(data.end(), fnsig.begin(), fnsig.end());

            // Parameter: id (refund_id as uint256) - 32 bytes
            vector<uint8_t> id_param(32, 0);
            for (int j = 7; j >= 0; j--) {
                id_param[24 + j] = static_cast<uint8_t>((refund_id >> (j * 8)) & 0xFF);
            }
            data.insert(data.end(), id_param.begin(), id_param.end());

            // Find the EVM account of this bridge contract
            account_table _accounts(evm_account, evm_account.value);
            auto accounts_byaccount = _accounts.get_index<"byaccount"_n>();
            auto evm_bridge_account = accounts_byaccount.find(get_self().value);
            
            if (evm_bridge_account != accounts_byaccount.end()) {
                // Get EVM config for gas price
                config_singleton_evm evm_config(evm_account, evm_account.value);
                config evm_conf;
                if (evm_config.exists()) {
                    evm_conf = evm_config.get();
                } else {
                    evm_conf = config();
                }

                // Call refundSuccessful() on EVM bridge contract with RLP encoding
                // Create lvalues for RLP encoding (required by rlp::encode)
                uint64_t nonce = evm_bridge_account->nonce;
                uint256_t gas_price = checksum256_to_uint256(evm_conf.gas_price);
                uint64_t gas_limit = evm_bridge::REFUND_CB_GAS;
                uint256_t value = 0;
                uint64_t chain_id = evm_bridge::CURRENT_CHAIN_ID;
                uint64_t r = 0;
                uint64_t s = 0;
                
                action(
                    permission_level{get_self(), "active"_n},
                    evm_account,
                    "raw"_n,
                    std::make_tuple(
                        get_self(),
                        rlp::encode(nonce, gas_price, gas_limit, evm_to, value, data, chain_id, r, s),
                        false,
                        std::optional<checksum160>(evm_bridge_account->address)
                    )
                ).send();
            }

            refunds.emplace(get_self(), [&](auto& row) {
                row.id = refunds.available_primary_key();
                row.refund_id = refund_id;
                row.asset_id = asset_id;
                row.owner = receiver;
                row.collection = collection;
                row.created_at = time_point_sec(current_time_point());
            });

            print("Refund:", refund_id, " asset_id=", asset_id, " owner=", receiver, " collection=", collection, "; ");
        }
    }

    // ======================== Helper Functions ========================

    vector<uint8_t> nftbridge::uint256_to_bytes(uint128_t low, uint128_t high) {
        return uint256_to_bytes_internal(low, high);
    }

    string nftbridge::get_nft_metadata(uint64_t asset_id) {
        // TODO: Read from AtomicAssets tables
        // Return IPFS URI: "ipfs://QmXxx..."
        return "ipfs://QmPlaceholder";
    }

    vector<uint8_t> nftbridge::get_collection_evm_address(name collection_name) {
        auto conf = config_bridge.get();

        account_state_table register_account_states(evm_account, conf.evm_register_scope);
        auto register_account_states_bykey = register_account_states.get_index<"bykey"_n>();

        // Get array slot to find PairNFT pairs[] array length
        auto pair_storage_key = make_storage_key(evm_bridge::STORAGE_REGISTER_PAIR_INDEX);
        auto pair_array_length_state = register_account_states_bykey.find(pair_storage_key);
        check(pair_array_length_state != register_account_states_bykey.end(), "No NFT pairs found in EVM register");

        uint64_t pair_array_length = uint256_to_uint64(pair_array_length_state->value_low, pair_array_length_state->value_high);
        check(pair_array_length > 0, "No NFT pairs registered");

        auto pair_array_slot = keccak256(checksum_to_bytes(pair_storage_key));
        const uint8_t pair_property_count = 7; // PairNFT struct has 7 properties

        // Find the pair for this collection
        for (uint64_t i = 0; i < pair_array_length; i++) {
            // Property 3: collectionName
            const auto collection_name_key = add_to_key(pair_array_slot, 3 + (pair_property_count * i));
            const auto collection_name_state = register_account_states_bykey.find(collection_name_key);

            if (collection_name_state != register_account_states_bykey.end()) {
                name stored_collection = name{decode_short_string(collection_name_state->value_low, collection_name_state->value_high)};

                if (stored_collection == collection_name) {
                    // Property 0: active
                    const auto pair_active_key = add_to_key(pair_array_slot, 0 + (pair_property_count * i));
                    const auto pair_active = register_account_states_bykey.find(pair_active_key);
                    check(pair_active != register_account_states_bykey.end() &&
                          uint256_to_uint64(pair_active->value_low, pair_active->value_high) == 1,
                          "This NFT collection's pair is paused");

                    // Property 2: evmAddress
                    const auto pair_evm_address_key = add_to_key(pair_array_slot, 2 + (pair_property_count * i));
                    const auto pair_evm_address_stored = register_account_states_bykey.find(pair_evm_address_key);
                    check(pair_evm_address_stored != register_account_states_bykey.end(), "Unable to find Pair EVM Address");

                    auto addr_bytes = uint256_to_bytes_internal(pair_evm_address_stored->value_low, pair_evm_address_stored->value_high);
                    // Keep only the last 20 bytes for the address
                    return vector<uint8_t>(addr_bytes.begin() + 12, addr_bytes.end());
                }
            }
        }

        return {};
    }
}
