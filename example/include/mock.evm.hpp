#pragma once
#include <eosio/eosio.hpp>
#include <eosio/crypto.hpp>
#include <optional>

using namespace eosio;

class [[eosio::contract("mock.evm")]] mockevm : public contract {
  public:
    using contract::contract;

    // Use two uint128 to represent uint256
    struct uint256 {
        uint128_t low;
        uint128_t high;
    };

    struct [[eosio::table]] account_state {
        uint64_t index;
        checksum256 key;
        uint128_t value_low;   // Lower 128 bits
        uint128_t value_high;  // Upper 128 bits
        
        uint64_t primary_key() const { return index; }
        checksum256 by_key() const { return key; }
    };
    
    using account_state_table = multi_index<
        "accountstate"_n,
        account_state,
        indexed_by<"bykey"_n, const_mem_fun<account_state, checksum256, &account_state::by_key>>
    >;

    struct [[eosio::table]] account {
        uint64_t index;
        checksum160 address;
        name account;
        uint64_t nonce;
        std::vector<uint8_t> code;
        checksum256 balance;

        uint64_t primary_key() const { return index; }
        uint64_t get_account_value() const { return account.value; }
        checksum256 by_address() const {
            std::array<uint8_t, 32> output = {};
            auto input_bytes = address.extract_as_byte_array();
            std::copy(std::begin(input_bytes), std::end(input_bytes), std::begin(output) + 12);
            return checksum256(output);
        }
    };

    using account_table = multi_index<
        "account"_n,
        account,
        indexed_by<"byaddress"_n, const_mem_fun<account, checksum256, &account::by_address>>,
        indexed_by<"byaccount"_n, const_mem_fun<account, uint64_t, &account::get_account_value>>
    >;

    [[eosio::action]]
    void setstate(uint64_t scope, checksum256 key, uint128_t value_low, uint128_t value_high);
    
    [[eosio::action]]
    void clearstate(uint64_t scope);

    [[eosio::action]]
    void setaccount(uint64_t index, checksum160 address, name account);

    [[eosio::action]]
    void raw(name caller, std::vector<uint8_t> tx, bool estimate, std::optional<checksum160> sender);
};