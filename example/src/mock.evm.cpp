#include <mock.evm.hpp>

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