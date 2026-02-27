#include <mock.evm.hpp>
#include "../include/constants.hpp"
#include <intx/base.hpp>
#include <rlp/rlp.hpp>
#include <array>

namespace {
    // Helper to convert hex string to bytes
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

// Decode RLP transaction to extract the data field (simplified)
// RLP format: [nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0]
// For mock purposes, we just need to extract the data field at index 5
std::vector<uint8_t> extract_data_from_rlp(const std::vector<uint8_t>& tx) {
    // Check if this looks like RLP (first byte indicates list)
    if (tx.empty() || tx[0] < 0xc0) {
        // Not RLP-encoded, return as-is (simple data format)
        return tx;
    }
    
    // For RLP list, skip the list prefix and parse items
    // This is a simplified parser that assumes standard transaction format
    size_t pos = 0;
    
    // Skip list prefix
    if (tx[0] >= 0xf8) {
        // Long list (length > 55 bytes)
        size_t len_of_len = tx[0] - 0xf7;
        pos = 1 + len_of_len;
    } else if (tx[0] >= 0xc0) {
        // Short list
        pos = 1;
    }
    
    // Skip first 5 fields (nonce, gasPrice, gasLimit, to, value)
    for (int i = 0; i < 5 && pos < tx.size(); i++) {
        uint8_t byte = tx[pos];
        if (byte < 0x80) {
            // Single byte
            pos++;
        } else if (byte < 0xb8) {
            // Short string
            size_t len = byte - 0x80;
            pos += 1 + len;
        } else if (byte < 0xc0) {
            // Long string
            size_t len_of_len = byte - 0xb7;
            size_t len = 0;
            for (size_t j = 0; j < len_of_len && pos + 1 + j < tx.size(); j++) {
                len = (len << 8) | tx[pos + 1 + j];
            }
            pos += 1 + len_of_len + len;
        } else {
            // Unexpected list, return original
            return tx;
        }
    }
    
    // Extract data field (6th element, index 5)
    if (pos >= tx.size()) {
        return tx; // Not enough data
    }
    
    uint8_t data_byte = tx[pos];
    if (data_byte < 0x80) {
        // Single byte data
        return std::vector<uint8_t>{data_byte};
    } else if (data_byte < 0xb8) {
        // Short string data
        size_t len = data_byte - 0x80;
        if (pos + 1 + len <= tx.size()) {
            return std::vector<uint8_t>(tx.begin() + pos + 1, tx.begin() + pos + 1 + len);
        }
    } else if (data_byte < 0xc0) {
        // Long string data
        size_t len_of_len = data_byte - 0xb7;
        size_t len = 0;
        for (size_t j = 0; j < len_of_len && pos + 1 + j < tx.size(); j++) {
            len = (len << 8) | tx[pos + 1 + j];
        }
        if (pos + 1 + len_of_len + len <= tx.size()) {
            return std::vector<uint8_t>(tx.begin() + pos + 1 + len_of_len, tx.begin() + pos + 1 + len_of_len + len);
        }
    }
    
    // If we can't parse, return original
    return tx;
}

[[eosio::action]]
void mockevm::raw(name caller, std::vector<uint8_t> tx, bool estimate, std::optional<checksum160> sender) {
    require_auth(caller);
    
    // Decode RLP transaction to extract data field
    std::vector<uint8_t> data = extract_data_from_rlp(tx);
    
    static const char* hex = "0123456789abcdef";
    std::string prefix;
    const size_t prefix_len = std::min<size_t>(4, data.size());
    prefix.reserve(prefix_len * 2);
    for (size_t i = 0; i < prefix_len; ++i) {
        prefix.push_back(hex[(data[i] >> 4) & 0x0F]);
        prefix.push_back(hex[data[i] & 0x0F]);
    }
    lastcall_singleton last(get_self(), get_self().value);
    last.set(lastcall{
        .caller = caller,
        .estimate = estimate,
        .sender = sender,
        .tx_size = static_cast<uint32_t>(tx.size()),
        .tx_prefix = prefix
    }, get_self());

    // Parse function selector and handle callbacks (using decoded data)
    if (data.size() >= 4) {
        // Check for requestSuccessful(uint256)
        auto request_sig = from_hex(evm_bridge::EVM_REQUEST_SUCCESSFUL_SIGNATURE);
        if (data.size() >= 36 && data[0] == request_sig[0] && data[1] == request_sig[1] && 
            data[2] == request_sig[2] && data[3] == request_sig[3]) {
            // Extract call_id from parameters (bytes 4-35)
            uint64_t call_id = 0;
            for (int i = 0; i < 8; ++i) {
                call_id = (call_id << 8) | data[28 + i];
            }
            
            // Delete request from storage by setting requests.length = 0
            // In a real implementation, we'd search and remove the specific request
            // For mock purposes, we'll just clear the requests array
            account_table accounts(get_self(), get_self().value);
            auto accounts_byaccount = accounts.get_index<"byaccount"_n>();
            auto bridge_account = accounts_byaccount.find(caller.value);
            
            if (bridge_account != accounts_byaccount.end()) {
                uint64_t scope = bridge_account->index;
                
                // Set requests array length to 0 (use constant from constants.hpp)
                std::array<uint8_t, 32> slot_bytes{};
                slot_bytes[31] = evm_bridge::STORAGE_BRIDGE_REQUEST_INDEX;
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
        // Check for refundSuccessful(uint256)
        else {
            auto refund_sig = from_hex(evm_bridge::EVM_REFUND_SUCCESSFUL_SIGNATURE);
            if (data.size() >= 36 && data[0] == refund_sig[0] && data[1] == refund_sig[1] && 
                data[2] == refund_sig[2] && data[3] == refund_sig[3]) {
                // Extract refund_id from parameters (bytes 4-35)
                uint64_t refund_id = 0;
                for (int i = 0; i < 8; ++i) {
                    refund_id = (refund_id << 8) | data[28 + i];
                }
                
                // Delete refund from storage by setting refunds.length = 0
                account_table accounts(get_self(), get_self().value);
                auto accounts_byaccount = accounts.get_index<"byaccount"_n>();
                auto bridge_account = accounts_byaccount.find(caller.value);
                
                if (bridge_account != accounts_byaccount.end()) {
                    uint64_t scope = bridge_account->index;
                    
                    // Set refunds array length to 0 (use constant from constants.hpp)
                    std::array<uint8_t, 32> slot_bytes{};
                    slot_bytes[31] = evm_bridge::STORAGE_BRIDGE_REFUND_INDEX;
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
}