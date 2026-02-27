#include <mock.evm.hpp>
#include <array>

namespace {
    // Helper to convert checksum256 to bytes
    std::array<uint8_t, 32> checksum_to_bytes(const checksum256& value) {
        return value.extract_as_byte_array();
    }

    checksum256 bytes_to_checksum(const std::array<uint8_t, 32>& bytes) {
        return checksum256(bytes);
    }

    // Keccak256 for storage slot calculation (simplified version)
    checksum256 keccak256_slot(uint64_t slot) {
        std::array<uint8_t, 32> bytes{};
        for (int i = 0; i < 8; ++i) {
            bytes[31 - i] = static_cast<uint8_t>((slot >> (i * 8)) & 0xFF);
        }
        // For mock purposes, we'll use a simple hash
        // In production, this would be actual keccak256
        auto hash = sha256(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return hash;
    }

    checksum256 add_to_key(const checksum256& base, uint64_t offset) {
        auto bytes = checksum_to_bytes(base);
        uint64_t carry = offset;
        for (int i = 31; i >= 0 && carry > 0; --i) {
            uint64_t sum = bytes[i] + carry;
            bytes[i] = static_cast<uint8_t>(sum & 0xFF);
            carry = sum >> 8;
        }
        return bytes_to_checksum(bytes);
    }
}

[[eosio::action]]
void mockevm::setstate(uint64_t scope, checksum256 key, uint128_t value_low, uint128_t value_high) {
    require_auth(get_self());
    
    account_state_table states(get_self(), scope);
    auto states_bykey = states.get_index<"bykey"_n>();
    auto existing = states_bykey.find(key);
    
    if (existing != states_bykey.end()) {
        states.modify(*existing, get_self(), [&](auto& row) {
            row.value_low = value_low;
            row.value_high = value_high;
        });
    } else {
        states.emplace(get_self(), [&](auto& row) {
            row.index = states.available_primary_key();
            row.key = key;
            row.value_low = value_low;
            row.value_high = value_high;
        });
    }
}

[[eosio::action]]
void mockevm::clearstate(uint64_t scope) {
    require_auth(get_self());
    
    account_state_table states(get_self(), scope);
    auto itr = states.begin();
    while (itr != states.end()) {
        itr = states.erase(itr);
    }
}

[[eosio::action]]
void mockevm::setaccount(uint64_t index, checksum160 address, name account) {
    require_auth(get_self());

    account_table accounts(get_self(), get_self().value);
    auto itr = accounts.find(index);

    std::array<uint8_t, 32> zero{};

    if (itr != accounts.end()) {
        accounts.modify(itr, get_self(), [&](auto& row) {
            row.address = address;
            row.account = account;
            row.nonce = row.nonce;
            row.code = row.code;
            row.balance = row.balance;
        });
    } else {
        accounts.emplace(get_self(), [&](auto& row) {
            row.index = index;
            row.address = address;
            row.account = account;
            row.nonce = 0;
            row.code = {};
            row.balance = checksum256(zero);
        });
    }
}

[[eosio::action]]
void mockevm::raw(name caller, std::vector<uint8_t> tx, bool estimate, std::optional<checksum160> sender) {
    require_auth(caller);
    static const char* hex = "0123456789abcdef";
    std::string prefix;
    const size_t prefix_len = std::min<size_t>(4, tx.size());
    prefix.reserve(prefix_len * 2);
    for (size_t i = 0; i < prefix_len; ++i) {
        prefix.push_back(hex[(tx[i] >> 4) & 0x0F]);
        prefix.push_back(hex[tx[i] & 0x0F]);
    }
    lastcall_singleton last(get_self(), get_self().value);
    last.set(lastcall{
        .caller = caller,
        .estimate = estimate,
        .sender = sender,
        .tx_size = static_cast<uint32_t>(tx.size()),
        .tx_prefix = prefix
    }, get_self());

    // Parse function selector and handle callbacks
    if (tx.size() >= 4) {
        // Check for requestSuccessful(uint256) - function selector: 0x7d9c16c9
        if (tx[0] == 0x7d && tx[1] == 0x9c && tx[2] == 0x16 && tx[3] == 0xc9 && tx.size() >= 36) {
            // Extract call_id from parameters (bytes 4-35)
            uint64_t call_id = 0;
            for (int i = 0; i < 8; ++i) {
                call_id = (call_id << 8) | tx[28 + i];
            }
            
            // Delete request from storage by setting requests.length = 0
            // In a real implementation, we'd search and remove the specific request
            // For mock purposes, we'll just clear the requests array
            account_table accounts(get_self(), get_self().value);
            auto accounts_byaccount = accounts.get_index<"byaccount"_n>();
            auto bridge_account = accounts_byaccount.find(caller.value);
            
            if (bridge_account != accounts_byaccount.end()) {
                uint64_t scope = bridge_account->index;
                
                // Set requests array length to 0 (slot 5)
                std::array<uint8_t, 32> slot_bytes{};
                slot_bytes[31] = 5;
                checksum256 length_key = bytes_to_checksum(slot_bytes);
                
                account_state_table states(get_self(), scope);
                auto states_bykey = states.get_index<"bykey"_n>();
                auto existing = states_bykey.find(length_key);
                
                if (existing != states_bykey.end()) {
                    states.modify(*existing, get_self(), [&](auto& row) {
                        row.value_low = 0;
                        row.value_high = 0;
                    });
                }
                
                print("Mock EVM: requestSuccessful(", call_id, ") - cleared requests array");
            }
        }
        // Check for refundSuccessful(uint256) - function selector: 0x8e198cf1
        else if (tx[0] == 0x8e && tx[1] == 0x19 && tx[2] == 0x8c && tx[3] == 0xf1 && tx.size() >= 36) {
            // Extract refund_id from parameters (bytes 4-35)
            uint64_t refund_id = 0;
            for (int i = 0; i < 8; ++i) {
                refund_id = (refund_id << 8) | tx[28 + i];
            }
            
            // Delete refund from storage by setting refunds.length = 0
            account_table accounts(get_self(), get_self().value);
            auto accounts_byaccount = accounts.get_index<"byaccount"_n>();
            auto bridge_account = accounts_byaccount.find(caller.value);
            
            if (bridge_account != accounts_byaccount.end()) {
                uint64_t scope = bridge_account->index;
                
                // Set refunds array length to 0 (slot 6)
                std::array<uint8_t, 32> slot_bytes{};
                slot_bytes[31] = 6;
                checksum256 length_key = bytes_to_checksum(slot_bytes);
                
                account_state_table states(get_self(), scope);
                auto states_bykey = states.get_index<"bykey"_n>();
                auto existing = states_bykey.find(length_key);
                
                if (existing != states_bykey.end()) {
                    states.modify(*existing, get_self(), [&](auto& row) {
                        row.value_low = 0;
                        row.value_high = 0;
                    });
                }
                
                print("Mock EVM: refundSuccessful(", refund_id, ") - cleared refunds array");
            }
        }
    }
}