#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/singleton.hpp>
#include <eosio/time.hpp>
#include <optional>
#include <string>
#include <vector>
#include <array>

namespace nft_bridge {

using eosio::checksum160;
using eosio::checksum256;
using eosio::current_time_point;
using eosio::check;
using eosio::name;
using eosio::print;
using eosio::require_auth;
using eosio::singleton;
using eosio::time_point_sec;
using eosio::multi_index;
using eosio::permission_level;
using eosio::action;
using std::optional;
using std::string;
using std::vector;
using std::array;

// Forward declarations for EVM types
namespace bigint {
    using checksum256 = eosio::checksum256;
}

struct [[eosio::table]] account_state {
    uint64_t index;
    checksum256 key;
    uint128_t value_low;
    uint128_t value_high;

    uint64_t primary_key() const { return index; }
    checksum256 by_key() const { return key; }
};

using account_state_table = multi_index<
    "accountstate"_n,
    account_state,
    eosio::indexed_by<"bykey"_n, eosio::const_mem_fun<account_state, checksum256, &account_state::by_key>>
>;

struct [[eosio::table]] Account {
    uint64_t index;
    checksum160 address;
    name account;
    uint64_t nonce;
    vector<uint8_t> code;
    bigint::checksum256 balance;

    uint64_t primary_key() const { return index; }
    uint64_t get_account_value() const { return account.value; }
    checksum256 by_address() const {
        array<uint8_t, 32> output = {};
        auto input_bytes = address.extract_as_byte_array();
        std::copy(std::begin(input_bytes), std::end(input_bytes), std::begin(output) + 12);
        return checksum256(output);
    }
};

using account_table = multi_index<
    "account"_n,
    Account,
    eosio::indexed_by<"byaddress"_n, eosio::const_mem_fun<Account, checksum256, &Account::by_address>>,
    eosio::indexed_by<"byaccount"_n, eosio::const_mem_fun<Account, uint64_t, &Account::get_account_value>>
>;

struct [[eosio::table]] config {
    uint32_t trx_index = 0;
    uint32_t last_block = 0;
    bigint::checksum256 gas_used_block;
    bigint::checksum256 gas_price;

    config() : trx_index(0), last_block(0) {
        // Initialize checksum256 fields to zero
        std::array<uint8_t, 32> zero{};
        gas_used_block = bigint::checksum256(zero);
        gas_price = bigint::checksum256(zero);
    }

    EOSLIB_SERIALIZE(config, (trx_index)(last_block)(gas_used_block)(gas_price))
};

using config_singleton_evm = singleton<"config"_n, config>;

class [[eosio::contract("nftbridge")]] nftbridge : public eosio::contract {
  public:
    using contract::contract;

    nftbridge(name receiver, name code, eosio::datastream<const char*> ds)
        : contract(receiver, code, ds),
          config_bridge(receiver, receiver.value) {}

    [[eosio::action]]
    void init(checksum160 bridge_address, checksum160 register_address, string version, name admin);

    [[eosio::action]]
    void modifyconfig(
        optional<checksum160> bridge_address,
        optional<checksum160> register_address,
        optional<name> admin,
        optional<string> version
    );

    [[eosio::action]]
    void clearerrorlog(optional<vector<uint64_t>> ids);

    [[eosio::on_notify("*::transfer")]]
    void bridge(name from, name to, vector<uint64_t> asset_ids, string memo);

    [[eosio::action]]
    void reqnotify();

    [[eosio::action]]
    void refundnotify();

  private:
    struct [[eosio::table("config")]] config_row {
        string version;
        name admin;
        checksum160 evm_bridge_address;
        checksum160 evm_register_address;
        uint64_t evm_bridge_scope;
        uint64_t evm_register_scope;
    };

    using config_singleton = singleton<"config"_n, config_row>;
    config_singleton config_bridge;

    struct [[eosio::table]] errorlog {
        uint64_t id;
        string message;
        time_point_sec created_at;

        uint64_t primary_key() const { return id; }
    };

    using errorlogs_table = multi_index<"errorlogs"_n, errorlog>;

    struct [[eosio::table]] request_row {
        uint64_t id;
        uint64_t call_id;
        string sender;
        uint64_t amount;
        string receiver;
        uint8_t evm_decimals;
        time_point_sec created_at;

        uint64_t primary_key() const { return id; }
        uint64_t by_timestamp() const { return created_at.sec_since_epoch(); }
    };

    using requests_table = multi_index<
        "requests"_n,
        request_row,
        eosio::indexed_by<"timestamp"_n, eosio::const_mem_fun<request_row, uint64_t, &request_row::by_timestamp>>
    >;

    struct [[eosio::table]] refund_row {
        uint64_t id;
        uint64_t refund_id;
        uint64_t asset_id;
        string owner;
        time_point_sec created_at;

        uint64_t primary_key() const { return id; }
        uint64_t by_timestamp() const { return created_at.sec_since_epoch(); }
    };

    using refunds_table = multi_index<
        "refunds"_n,
        refund_row,
        eosio::indexed_by<"timestamp"_n, eosio::const_mem_fun<refund_row, uint64_t, &refund_row::by_timestamp>>
    >;

    static constexpr name evm_account = "eosio.evm"_n;

    vector<uint8_t> uint256_to_bytes(uint128_t low, uint128_t high);
    string get_nft_metadata(uint64_t asset_id);
    vector<uint8_t> get_collection_evm_address(name collection_name);
};

} // namespace nft_bridge
