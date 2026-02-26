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
    (void)caller;
    (void)tx;
    (void)estimate;
    (void)sender;
}