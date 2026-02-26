#pragma once
#include <eosio/eosio.hpp>
#include <eosio/crypto.hpp>

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

    [[eosio::action]]
    void setstate(uint64_t scope, checksum256 key, uint128_t value_low, uint128_t value_high);
    
    [[eosio::action]]
    void clearstate(uint64_t scope);
};